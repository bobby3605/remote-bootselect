#include "common.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <linux/filter.h>

// https://natanyellin.com/posts/ebpf-filtering-done-right/
/*
void drain_socket(int socket) {
    sock_filter zero_bytecode = BPF_STMT(BPF_RET | BPF_K, 0);
    sock_fprog zero_program = {1, &zero_bytecode};
    if (setsockopt(socket, SOL_SOCKET, SO_ATTACH_FILTER, &zero_program, sizeof(zero_program)) != 0) {
        std::cout << "error attaching zero bpf: " << strerror(errno) << std::endl;
        exit(errno);
    }
    char drain[1];
    while (true) {
        int bytes = recv(socket, drain, sizeof(drain), MSG_DONTWAIT);
        if (bytes == -1) {
            break;
        }
    }
}
*/

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

void print_mac(MAC const& mac) {
    for (size_t i = 0; i < mac.size(); i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)mac[i];
        if (i != mac.size() - 1) {
            std::cout << ":";
        }
    }
    std::cout << std::dec;
}
