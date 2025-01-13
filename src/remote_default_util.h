#ifndef REMOTE_DEFAULT_UTIL_H_
#define REMOTE_DEFAULT_UTIL_H_
#include <net/ethernet.h>

struct RemoteDefaultRequest {
  char grub[5];
} RemoteDefaultRequest_grub = {"grub"};

struct RequestFrame {
  struct ethhdr hdr;
  struct RemoteDefaultRequest request;
};

struct DataPacket {
  struct RemoteDefaultRequest request;
  unsigned char default_entry;
};

struct DataFrame {
  struct ethhdr hdr;
  struct DataPacket packet;
};

struct RemoteDefaultData {
  unsigned char mac[6];
  unsigned char default_entry;
};

struct RemoteDefaultNode {
  struct RemoteDefaultData data;
  struct RemoteDefaultNode *next;
};

#endif // REMOTE_DEFAULT_UTIL_H_
