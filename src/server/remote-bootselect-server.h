#ifndef REMOTE_DEFAULT_UTIL_H_
#define REMOTE_DEFAULT_UTIL_H_
#include <net/ethernet.h>

#define BOOTSELECT_ETHERTYPE 0x7184
#define DEFAULT_ENTRY_MAX_LENGTH 64

struct uint48 {
  uint64_t x : 48;
} __attribute__((packed));

struct RequestFrame {
  struct ethhdr hdr;
};

struct DataFrame {
  struct ethhdr hdr;
  char default_entry[DEFAULT_ENTRY_MAX_LENGTH];
};

struct RemoteDefaultData {
  struct uint48 mac;
  char default_entry[DEFAULT_ENTRY_MAX_LENGTH];
};

struct RemoteDefaultEntries {
  struct RemoteDefaultData *data;
  unsigned int size;
};

#endif // REMOTE_DEFAULT_UTIL_H_
