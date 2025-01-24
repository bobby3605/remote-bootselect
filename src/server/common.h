#ifndef COMMON_H_
#define COMMON_H_
#include <stdint.h>

#define DEFAULT_ENTRY_MAX_LENGTH 64

struct uint48 {
  uint64_t x : 48;
} __attribute__((packed));

struct RemoteDefaultData {
  struct uint48 mac;
  char default_entry[DEFAULT_ENTRY_MAX_LENGTH];
};

struct RemoteDefaultEntries {
  struct RemoteDefaultData *data;
  unsigned int size;
};

extern struct RemoteDefaultEntries remote_default_entries;

#endif // COMMON_H_
