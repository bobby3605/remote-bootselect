#pragma once
#include <array>
#include <istream>
#include <linux/filter.h>
#include <net/ethernet.h>
#include <unordered_map>

const uint16_t ETHERTYPE = 0x7184;
const uint64_t MAX_ENTRY_LENGTH = 255;

using MAC = std::array<unsigned char, 6>;
const MAC ether_broadcast_addr = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct RequestFrame {
    ethhdr hdr;
};

struct __attribute__((packed)) DataFrame {
    ethhdr hdr;
    uint8_t entry_length;
    char entry[MAX_ENTRY_LENGTH];
};

void drain_socket(int socket);

bool parse_mac(std::istream& config, MAC& mac);
void print_mac(MAC& mac);

namespace std {
template <> struct hash<MAC> {
    std::size_t operator()(const MAC& mac) const {
        std::size_t h = 0;
        for (auto b : mac) {
            h = (h << 8) ^ b;
        }
        return h;
    }
};
} // namespace std

extern std::unordered_map<MAC, std::string> defaultEntries;
