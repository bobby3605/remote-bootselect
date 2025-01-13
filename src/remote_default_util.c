#include "remote_default_util.h"
#include <arpa/inet.h>
#include <errno.h>
#include <malloc.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

struct RemoteDefaultNode *remote_default_list;

int setup_socket() {
  int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
  if (sock == -1) {
    printf("%s", strerror(errno));
    exit(errno);
  }
  return sock;
}

void get_if_info(int sock, char *if_name, int *index, struct sockaddr *hwaddr) {
  size_t if_name_len = strlen(if_name);
  struct ifreq ifr;
  if (if_name_len < sizeof(ifr.ifr_name)) {
    memcpy(ifr.ifr_name, if_name, if_name_len);
    ifr.ifr_name[if_name_len] = 0;
  } else {
    printf("%s", strerror(errno));
    exit(errno);
  }

  if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
    printf("%s", strerror(errno));
    exit(errno);
  }
  *index = ifr.ifr_ifindex;

  if (ioctl(sock, SIOCGIFHWADDR, &ifr) == -1) {
    printf("%s", strerror(errno));
    exit(errno);
  }
  memcpy(hwaddr->sa_data, ifr.ifr_hwaddr.sa_data,
         sizeof(ifr.ifr_hwaddr.sa_data));
}

void construct_eth_hdr(struct ethhdr *frame, unsigned char *dest,
                       unsigned char *src, unsigned int len_or_type) {
  memcpy(frame->h_dest, dest, ETH_ALEN);
  memcpy(frame->h_source, src, ETH_ALEN);
  frame->h_proto = htons(len_or_type);
}

void send_packet(int sock, char *if_name, void *dst_addr, void *packet,
                 size_t packet_len) {
  struct sockaddr_ll addr = {0};
  int ifindex;
  struct sockaddr hwaddr;
  get_if_info(sock, if_name, &ifindex, &hwaddr);
  addr.sll_family = AF_PACKET;
  addr.sll_ifindex = ifindex;
  addr.sll_halen = ETHER_ADDR_LEN;
  addr.sll_protocol = htons(ETH_P_ALL);
  memcpy(addr.sll_addr, dst_addr, ETHER_ADDR_LEN);

  size_t frame_size = sizeof(struct ethhdr) + packet_len;
  unsigned char *frame = malloc(frame_size);

  construct_eth_hdr((struct ethhdr *)frame, dst_addr,
                    (unsigned char *)hwaddr.sa_data, packet_len);
  memcpy(frame + sizeof(struct ethhdr), packet, packet_len);

  if (sendto(sock, frame, frame_size, 0, (struct sockaddr *)&addr,
             sizeof(addr)) == -1) {
    printf("%s", strerror(errno));
    free(frame);
    exit(errno);
  }
  free(frame);
}

void request_default(int sock, char *if_name) {
  unsigned char ether_broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  struct RemoteDefaultRequest request = RemoteDefaultRequest_grub;
  send_packet(sock, if_name, ether_broadcast_addr, &request, sizeof(request));
  struct DataFrame frame;
  while (strcmp(frame.packet.request.grub, request.grub) != 0) {
    recvfrom(sock, &frame, sizeof(frame), 0, NULL, NULL);
  }
  printf("got default entry: %d", frame.packet.default_entry);
}

void listen_default(int sock, char *if_name) {
  struct RequestFrame frame;
  struct RemoteDefaultRequest request = RemoteDefaultRequest_grub;
  while (strcmp(frame.request.grub, request.grub) != 0) {
    recvfrom(sock, &frame, sizeof(frame), 0, NULL, NULL);
  }
  printf("received: %s from: ", frame.request.grub);
  for (int i = 0; i < sizeof(frame.hdr.h_source); ++i) {
    printf("%02X:", frame.hdr.h_source[i]);
  }
  printf("\n");
  struct RemoteDefaultNode *node = remote_default_list;
  while (node) {
    if (memcmp(node->data.mac, frame.hdr.h_source, sizeof(node->data.mac)) ==
        0) {
      struct DataPacket return_packet = {"grub", node->data.default_entry};
      send_packet(sock, if_name, node->data.mac, &return_packet,
                  sizeof(return_packet));
      printf("sent with default: %d", return_packet.default_entry);
      break;
    } else {
      node = node->next;
    }
  }
  // runs when break wasn't called in the for loop
  if (node == NULL) {
    printf("failed to find entry for mac address: ");
    for (int i = 0; i < sizeof(frame.hdr.h_source); ++i) {
      printf("%02X:", frame.hdr.h_source[i]);
    }
    printf("\n");
    printf("list: \n");
    node = remote_default_list;
    while (node) {
      for (int i = 0; i < sizeof(node->data.mac); ++i) {
        printf("%02X:", node->data.mac[i]);
      }
      printf(" %x \n", node->data.default_entry);
      node = node->next;
    }
    exit(1);
  }
}

void load_defaults(int argc, char *argv[]) {
  remote_default_list = malloc(sizeof(struct RemoteDefaultNode));
  struct RemoteDefaultNode *node = remote_default_list;
  for (int i = 3; i < argc;) {
    memset(node, 0, sizeof(*node));
    char *arg_mac = argv[i++];
    int pos = 0;
    for (int j = 0; j < sizeof(node->data.mac); ++j) {
      sscanf(arg_mac + pos, "%2hhx", node->data.mac + j);
      pos += 2;
    }
    sscanf(argv[i++], "%hhx", &node->data.default_entry);
    node->next = malloc(sizeof(struct RemoteDefaultNode));
  }
  // free the extra allocation
  free(node->next);
  node->next = NULL;
}

int main(int argc, char *argv[]) {
  int sock = setup_socket();
  char *if_name = argv[1];
  if (strcmp(argv[2], "request") == 0) {
    printf("sending request \n");
    request_default(sock, if_name);
  } else if (strcmp(argv[2], "listen") == 0) {
    printf("listening for request \n");
    load_defaults(argc, argv);
    listen_default(sock, if_name);
  } else {
    printf("valid usage: ./remote_default_util interface_name request\n");
    printf("valid usage: ./remote_default_util interface_name listen [[mac] "
           "[default_entry]]...");
  }
  close(sock);
}
