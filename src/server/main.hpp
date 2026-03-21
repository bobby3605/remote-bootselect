#pragma once
#include <array>
#include <linux/filter.h>
#include <net/ethernet.h>

const uint16_t ETHERTYPE = 0x7184;
const uint64_t MAX_ENTRY_LENGTH = 64;

using MAC = std::array<unsigned char, 6>;
const MAC ether_broadcast_addr = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct RequestFrame {
    ethhdr hdr;
};

struct DataFrame {
    ethhdr hdr;
    uint64_t entry_length;
    char entry[MAX_ENTRY_LENGTH];
};
