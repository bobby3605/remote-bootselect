#include "RequestHandler.hpp"
#include "common.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

std::array<sock_filter, 4> filter_code = {
    sock_filter{0x28, 0, 0, 0x0000000c}, {0x15, 0, 1, ETHERTYPE}, {0x6, 0, 0, sizeof(DataFrame)}, {0x6, 0, 0, 0x00000000}};

const sock_fprog filter = {
    .len = filter_code.size(),
    .filter = &filter_code.at(0),
};

RequestHandler::RequestHandler(EventHandler& eventHandler, std::string const& interface) {
    create_data_socket();
    if (data_socket != -1) {
        handler = std::bind(&RequestHandler::process_socket, this);
        eventHandler.register_socket(data_socket, handler);
    } else {
        std::cout << "error: failed to create data socket: " << strerror(errno) << std::endl;
        exit(errno);
    }
    get_if_info(interface);
}

RequestHandler::~RequestHandler() { close(data_socket); }

void RequestHandler::create_data_socket() {
    data_socket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (data_socket != -1) {
        drain_socket(data_socket);
        if (setsockopt(data_socket, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter)) != 0) {
            std::cout << "error: failed to attach data socket filter: " << strerror(errno) << std::endl;
            exit(errno);
        }
    }
}

void RequestHandler::get_if_info(std::string const& interface) {
    ifreq ifr;
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

void RequestHandler::process_socket() {

    RequestFrame source_frame = {};
    recv(data_socket, &source_frame, sizeof(source_frame), 0);
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
        // sll_addr doesn't matter, because it's set in the header
        if (sendto(data_socket, &data, send_size, 0, (const sockaddr*)&addr, (socklen_t)sizeof(addr)) == -1) {
            std::cout << "failed to send packet: " << strerror(errno) << std::endl;
        }
    } else {
        std::cout << "failed to find entry for MAC: ";
        print_mac(src_addr);
        std::cout << std::endl;
    }
}
