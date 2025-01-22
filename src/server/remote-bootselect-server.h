#ifndef REMOTE_DEFAULT_UTIL_H_
#define REMOTE_DEFAULT_UTIL_H_
#include "common.h"
#include <net/ethernet.h>

#define BOOTSELECT_ETHERTYPE 0x7184

enum EPOLL_DATA { LISTEN_SOCKET, CONFIG_FIFO };

struct RequestFrame {
  struct ethhdr hdr;
};

struct DataFrame {
  struct ethhdr hdr;
  char default_entry[DEFAULT_ENTRY_MAX_LENGTH];
};

#endif // REMOTE_DEFAULT_UTIL_H_
