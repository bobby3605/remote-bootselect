#include "RequestHandler.hpp"
#include "common.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <linux/if_packet.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/*
std::array<sock_filter, 4> filter_code = {
    sock_filter{0x28, 0, 0, 0x0000000c       },
    {0x15, 0, 1, ETHERTYPE        },
    {0x6,  0, 0, sizeof(DataFrame)},
    {0x6,  0, 0, 0x00000000       }
};

const sock_fprog filter = {
    .len = filter_code.size(),
    .filter = &filter_code.at(0),
};
*/

RequestHandler::RequestHandler(EventHandler& eventHandler, MQTTHandler& mqttHandler, std::string const& interface)
    : mqttHandler(mqttHandler) {
    create_data_socket();
    if (data_socket != -1) {
        handler = std::bind(&RequestHandler::process_socket, this, std::placeholders::_1);
        eventHandler.register_socket(data_socket, handler);
    } else {
        std::cout << "error: failed to create data socket: " << strerror(errno) << std::endl;
        exit(errno);
    }
    get_if_info(interface);
}

RequestHandler::~RequestHandler() {
    if (data_socket != -1) {
        close(data_socket);
    }
}

void RequestHandler::create_data_socket() {
    data_socket = socket(AF_PACKET, SOCK_RAW, htons(ETHERTYPE));
    if (data_socket != -1) {
        /*
        drain_socket(data_socket);
        if (setsockopt(data_socket, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter)) != 0) {
            std::cout << "error: failed to attach data socket filter: " << strerror(errno) << std::endl;
            exit(errno);
        }
        */
    } else {
        std::cout << "error: failed to create L2 socket: " << strerror(errno) << std::endl;
    }
}

void RequestHandler::get_if_info(std::string const& interface) {
    ifreq ifr = {};
    if (interface.size() < sizeof(ifr.ifr_name)) {
        memcpy(ifr.ifr_name, interface.data(), interface.size());
        ifr.ifr_name[interface.size()] = 0;
    } else {
        std::cout << "bad length when getting interface info: " << strerror(errno) << std::endl;
        exit(errno);
    }

    if (ioctl(data_socket, SIOCGIFINDEX, &ifr) == -1) {
        std::cout << "failed to get interface " << interface.data() << " " << strerror(errno) << std::endl;
        exit(errno);
    }
    ifindex = ifr.ifr_ifindex;

    if (ioctl(data_socket, SIOCGIFHWADDR, &ifr) == -1) {
        std::cout << "failed to get interface mac address: " << strerror(errno) << std::endl;
        exit(errno);
    }
    memcpy(hwaddr.data(), ifr.ifr_hwaddr.sa_data, hwaddr.size());
}

void RequestHandler::process_socket(uint32_t /*events*/) {
    size_t bufsize = 0;
    if (ioctl(data_socket, FIONREAD, &bufsize) < 0) {
        std::cout << "warning: failed to get buffer size for data socket: " << strerror(errno) << std::endl;
        return;
    }
    std::vector<unsigned char> frame(bufsize);
    int r = recv(data_socket, frame.data(), bufsize, 0);
    if (r == -1) {
        std::cout << "warning: failed to receive frame: " << strerror(errno) << std::endl;
        return;
    } else if (r != (int)bufsize) {
        std::cout << "warning: unexpected frame receive size: " << r << std::endl;
        return;
    }
    // NOTE:
    // handling the case where the L2 packet was extended to 60 bytes
    if (bufsize == sizeof(RequestFrame) || (bufsize > sizeof(RequestFrame) && frame[sizeof(RequestFrame)] == '\0')) {
        process_request(frame);
    } else {
        process_menuentries(frame);
    }
}

void RequestHandler::process_request(std::vector<unsigned char> const& frame) {
    RequestFrame source_frame = {};
    std::memcpy(&source_frame, frame.data(), sizeof(source_frame));
    // check that it is a broadcast packet
    if (memcmp(source_frame.hdr.h_dest, ether_broadcast_addr.data(), ether_broadcast_addr.size()) != 0) {
        return;
    }

    MAC src_addr = {};
    std::memcpy(src_addr.data(), source_frame.hdr.h_source, src_addr.size());
    auto entryIt = defaultEntries.find(src_addr);
    if (entryIt != defaultEntries.end()) {
        std::string& entry = entryIt->second;
        if (entry.size() > MAX_ENTRY_LENGTH) {
            std::cout << "error: entry for ";
            print_mac(src_addr);
            std::cout << " is too large: " << entry.size() << std::endl;
            return;
        }

        DataFrame data = {};
        data.hdr = source_frame.hdr;
        std::memcpy(data.hdr.h_dest, source_frame.hdr.h_source, sizeof(MAC));
        std::memcpy(data.hdr.h_source, hwaddr.data(), sizeof(MAC));
        data.entry_length = entry.size();
        std::memcpy(data.entry, entry.data(), data.entry_length);
        size_t send_size = offsetof(DataFrame, entry) + data.entry_length;

        sockaddr_ll addr = {};
        addr.sll_family = AF_PACKET;
        addr.sll_ifindex = ifindex;
        addr.sll_halen = ETHER_ADDR_LEN;
        addr.sll_protocol = htons(ETH_P_ALL);
        // NOTE:
        // sll_addr probably doesn't matter, because it's set in the header
        std::memcpy(addr.sll_addr, data.hdr.h_dest, sizeof(MAC));
        if (sendto(data_socket, &data, send_size, 0, (const sockaddr*)&addr, (socklen_t)sizeof(addr)) == -1) {
            std::cout << "failed to send packet: " << strerror(errno) << std::endl;
        }
    } else {
        std::cout << "failed to find entry for MAC: ";
        print_mac(src_addr);
        std::cout << std::endl;
    }
}

std::optional<std::string> read_strnlen(const char*& strbuf, int& remaining_len) {
    if (remaining_len == 0) {
        std::cout << "warning: got 0 remaining_length on read_strnlen" << std::endl;
        return {};
    }
    int len = strnlen(strbuf, remaining_len);
    if (len != 0 && remaining_len >= (len + 1)) {
        // + 1 for null terminator
        remaining_len -= (len + 1);
    } else {
        std::cout << "warning: got bad length on read_strnlen: " << len << std::endl;
        return {};
    }
    std::string str(strbuf);
    strbuf += (len + 1);
    return str;
}

void RequestHandler::process_menuentries(std::vector<unsigned char> const& frame) {
    ethhdr hdr;
    std::memcpy(&hdr, frame.data(), sizeof(hdr));

    const char* entry = reinterpret_cast<const char*>(frame.data()) + sizeof(hdr);
    int remaining_len = frame.size() - sizeof(hdr);

    std::unordered_map<std::string, std::string> menuentries;
    while (remaining_len != 0 && *entry != '\0') {
        auto id = read_strnlen(entry, remaining_len);
        auto title = read_strnlen(entry, remaining_len);

        if (id.has_value() && title.has_value()) {
            menuentries[id.value()] = title.value();
        } else {
            std::cout << "warning: process_menuentries: invalid id or title read" << std::endl;
            return;
        }
    }

    MAC mac = {};
    std::memcpy(mac.data(), hdr.h_source, mac.size());
    mqttHandler.upload_menuentries(mac, menuentries);
}
