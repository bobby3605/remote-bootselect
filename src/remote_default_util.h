#ifndef REMOTE_DEFAULT_UTIL_H_
#define REMOTE_DEFAULT_UTIL_H_
#include <net/ethernet.h>

struct RemoteDefaultRequest {
  char grub[5];
} RemoteDefaultRequest_grub = {"grub"};

struct RequestPacket {
  struct ethhdr hdr;
  struct RemoteDefaultRequest request;
};

#endif // REMOTE_DEFAULT_UTIL_H_
