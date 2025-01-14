#ifndef REMOTE_DEFAULT_UTIL_H_
#define REMOTE_DEFAULT_UTIL_H_
#include <net/ethernet.h>

#define BOOTSELECT_ETHERTYPE 0x7184

struct uint48 {
  uint64_t x : 48;
} __attribute__((packed));

struct RequestFrame {
  struct ethhdr hdr;
};

struct DataFrame {
  struct ethhdr hdr;
  unsigned char default_entry;
};

struct RemoteDefaultData {
  struct uint48 mac;
  unsigned char default_entry;
};

struct RemoteDefaultEntries {
  struct RemoteDefaultData *data;
  unsigned int size;
};

#endif // REMOTE_DEFAULT_UTIL_H_
