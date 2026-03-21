#include "main.hpp"
#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sstream>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace std {
template <> struct hash<MAC> {
    std::size_t operator()(const MAC& mac) const {
        std::size_t h = 0;
        for (auto b : mac) {
            b = (h << 8) ^ b;
        }
        return h;
    }
};
} // namespace std

std::unordered_map<MAC, std::string> defaultEntries;

bool parse_mac(std::istream& config, MAC& mac) {
    char c[2];
    int idx = 0;
    while (config.get(c[0]) && config.get(c[1])) {
        char c3;
        if (config.get(c3)) {
            if (c3 == ':' || c3 == ' ') {
                sscanf(c, "%2hhx", &mac[idx++]);
                if (idx == 6) {
                    return true;
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    return false;
}

void process_config(std::istream& config) {
    MAC mac;
    std::string entry;
    entry.reserve(MAX_ENTRY_LENGTH);
    while (!config.eof()) {
        if (parse_mac(config, mac)) {
            std::getline(config, entry);
            if (config.fail()) {
                std::cout << "warning: configuration failure" << std::endl;
                return;
            } else {
                defaultEntries[mac] = entry;
            }
        }
    }
}

int create_config_listener(std::string path) {
    int config_socket = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (config_socket != -1) {
        sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path.data(), path.size());
        addr.sun_path[path.size()] = '\0';
        // ensure that the socket no longer exists
        // linux doesn't automatically clean up unix sockets
        unlink(path.data());
        if (bind(config_socket, (sockaddr*)&addr, sizeof(addr)) == -1) {
            std::cout << "warning: failed to bind socket: " << path << std::endl;
        }
    } else {
        std::cout << "warning: failed to create config socket" << std::endl;
        return -1;
    }
    return config_socket;
}

// https://natanyellin.com/posts/ebpf-filtering-done-right/
void drain_socket(int sock) {
    sock_filter zero_bytecode = BPF_STMT(BPF_RET | BPF_K, 0);
    sock_fprog zero_program = {1, &zero_bytecode};
    if (setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &zero_program, sizeof(zero_program)) != 0) {
        std::cout << "error attaching zero bpf: " << strerror(errno) << std::endl;
        exit(errno);
    }
    char drain[1];
    while (true) {
        int bytes = recv(sock, drain, sizeof(drain), MSG_DONTWAIT);
        if (bytes == -1) {
            break;
        }
    }
}

std::array<sock_filter, 4> filter_code = {
    sock_filter{0x28, 0, 0, 0x0000000c}, {0x15, 0, 1, ETHERTYPE}, {0x6, 0, 0, sizeof(DataFrame)}, {0x6, 0, 0, 0x00000000}};

const sock_fprog filter = {
    .len = filter_code.size(),
    .filter = &filter_code.at(0),
};

int create_data_socket() {
    int data_socket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (data_socket != -1) {
        drain_socket(data_socket);
        if (setsockopt(data_socket, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter)) != 0) {
            std::cout << "error: failed to attach data socket filter: " << strerror(errno) << std::endl;
            exit(errno);
        }
    } else {
        std::cout << "error: failed to create data socket" << std::endl;
        exit(errno);
    }
    return data_socket;
}

int setup_epoll(std::vector<int> const& sockets) {
    int epfd = epoll_create1(0);
    epoll_event event;
    event.events = EPOLLIN;
    for (auto const& socket : sockets) {
        event.data.fd = socket;
        epoll_ctl(epfd, EPOLL_CTL_ADD, event.data.fd, &event);
    }
    return epfd;
}

sockaddr hwaddr = {};
int ifindex = -1;

void handle_requests(int epfd, int config_socket, int data_socket) {
    epoll_event events[2];
    while (true) {
        int event_count = epoll_wait(epfd, events, std::size(events), -1);
        for (int i = 0; i < event_count; i++) {
            if (events[i].data.fd == config_socket) {
                size_t bufsize = 0;
                if (ioctl(config_socket, FIONREAD, &bufsize) < 0) {
                    std::cout << "failed to get buffer size for config socket: " << strerror(errno) << std::endl;
                    std::exit(errno);
                }
                std::string buffer;
                buffer.resize(bufsize);
                int config_size = read(config_socket, buffer.data(), bufsize);
                if (config_size == -1) {
                    std::cout << "config recv failed: " << strerror(errno) << std::endl;
                    std::exit(errno);
                }
                std::stringstream stream(buffer);
                process_config(stream);

            } else if (events[i].data.fd == data_socket) {
                RequestFrame source_frame = {};
                recv(data_socket, &source_frame, sizeof(source_frame), 0);
                // check that it is a broadcast packet
                if (memcmp(source_frame.hdr.h_dest, ether_broadcast_addr.data(), ether_broadcast_addr.size()) != 0) {
                    continue;
                }

                DataFrame data = {};
                data.hdr = source_frame.hdr;
                std::memcpy(data.hdr.h_dest, source_frame.hdr.h_source, sizeof(MAC));
                std::memcpy(data.hdr.h_source, hwaddr.sa_data, sizeof(MAC));

                MAC src_addr = {};
                std::memcpy(src_addr.data(), source_frame.hdr.h_source, src_addr.size());
                std::string& entry = defaultEntries[src_addr];
                data.entry_length = entry.size();

                if (entry.length() > 0) {
                    std::memcpy(data.entry, entry.data(), data.entry_length);
                    sockaddr_ll addr = {};
                    addr.sll_family = AF_PACKET;
                    addr.sll_ifindex = ifindex;
                    addr.sll_halen = ETHER_ADDR_LEN;
                    addr.sll_protocol = htons(ETH_P_ALL);
                    memcpy(addr.sll_addr, source_frame.hdr.h_dest, ETHER_ADDR_LEN);
                    if (sendto(data_socket, &data, sizeof(data), 0, (const sockaddr*)&addr, (socklen_t)sizeof(addr)) == -1) {
                        std::cout << "failed to send packet: " << strerror(errno) << std::endl;
                    }
                } else {
                    std::cout << "failed to find entry for MAC: ";
                    for (size_t i = 0; i < src_addr.size(); i++) {
                        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)src_addr[i];
                        if (i != src_addr.size() - 1) {
                            std::cout << ":";
                        }
                    }

                    std::cout << std::dec << std::endl;
                }
            }
        }
    }
}

void get_if_info(int sock, std::string if_name, int* index, struct sockaddr* hwaddr) {
    ifreq ifr;
    if (if_name.size() < sizeof(ifr.ifr_name)) {
        memcpy(ifr.ifr_name, if_name.data(), if_name.size());
        ifr.ifr_name[if_name.size()] = 0;
    } else {
        std::cout << "bad length when getting interface info: " << strerror(errno) << std::endl;
    }

    if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
        std::cout << "failed to get interface " << if_name.data() << " " << strerror(errno) << std::endl;
    }
    *index = ifr.ifr_ifindex;

    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == -1) {
        std::cout << "failed to get interface mac address: " << strerror(errno) << std::endl;
    }
    memcpy(hwaddr->sa_data, ifr.ifr_hwaddr.sa_data, sizeof(ifr.ifr_hwaddr.sa_data));
}

int main(int argc, char* argv[]) {
    std::string ifname;
    int config_socket = -1;
    int data_socket = -1;
    for (int i = 0; i + 1 < argc; i++) {
        if (std::strcmp("-i", argv[i]) == 0) {
            ifname = std::string(argv[++i]);
        } else if (std::strcmp("-c", argv[i]) == 0) {
            std::ifstream config(argv[++i], std::ios::in);
            if (config.is_open()) {
                process_config(config);
                config.close();
            } else {
                std::cout << "failed to open config file: " << argv[i] << std::endl;
            }
        } else if (std::strcmp("-cl", argv[i]) == 0) {
            config_socket = create_config_listener(argv[++i]);
        }
    }

    if (config_socket == -1) {
        create_config_listener("/tmp/remote-bootselect.sock");
    }
    if (ifname.size() == 0) {
        std::cout << "error: interface option missing" << std::endl;
    } else {
        data_socket = create_data_socket();
        get_if_info(data_socket, ifname, &ifindex, &hwaddr);
        int epfd = setup_epoll({config_socket, data_socket});
        handle_requests(epfd, config_socket, data_socket);
    }

    if (config_socket != -1) {
        close(config_socket);
    }
    if (data_socket != -1) {
        close(data_socket);
    }
}
