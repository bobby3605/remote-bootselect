#include "remote_default_util.h"
#include <arpa/inet.h>
#include <errno.h>
#include <linux/filter.h>
#include <malloc.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

struct RemoteDefaultEntries remote_default_entries;

unsigned char ether_broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

const uint32_t max_size = sizeof(struct DataFrame);

struct sock_filter filter_code[] = {
    {0x28, 0, 0, 0x0000000c},
    {0x15, 0, 1, BOOTSELECT_ETHERTYPE},
    {0x6, 0, 0, sizeof(struct DataFrame)},
    {0x6, 0, 0, 0x00000000},
};

struct sock_fprog filter = {
    .len = sizeof(filter_code) / sizeof(filter_code[0]),
    .filter = filter_code,
};

void print_mac(void *mac) {
  unsigned char *mac_uc = (unsigned char *)mac;
  for (int i = 0; i < 6; ++i) {
    // don't print an extra : at the end
    if (i != 5) {
      printf("%02X:", mac_uc[i]);
    } else {
      printf("%02X", mac_uc[i]);
    }
  }
}

// https://natanyellin.com/posts/ebpf-filtering-done-right/
// https://github.com/the-tcpdump-group/libpcap/blob/f4fcc9396dc425399846cf082f9ed1056b81dd11/pcap-linux.c#L6096
void drain_socket(int sock) {
  struct sock_filter zero_bytecode = BPF_STMT(BPF_RET | BPF_K, 0);
  struct sock_fprog zero_program = {1, &zero_bytecode};
  if (setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &zero_program,
                 sizeof(zero_program)) != 0) {
    printf("error attaching zero bpf: %s\n", strerror(errno));
    exit(1);
  }
  char drain[1];
  while (true) {
    int bytes = recv(sock, drain, sizeof(drain), MSG_DONTWAIT);
    if (bytes == -1) {
      break;
    }
  }
}

int setup_socket() {
  int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
  if (sock == -1) {
    printf("%s", strerror(errno));
    exit(errno);
  }
  drain_socket(sock);
  if (setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter)) !=
      0) {
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
                       unsigned char *src) {
  memcpy(frame->h_dest, dest, ETH_ALEN);
  memcpy(frame->h_source, src, ETH_ALEN);
  frame->h_proto = htons(BOOTSELECT_ETHERTYPE);
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
                    (unsigned char *)hwaddr.sa_data);
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
  send_packet(sock, if_name, ether_broadcast_addr, NULL, 0);
  struct DataFrame frame = {0};
  int ifindex;
  struct sockaddr hwaddr;
  get_if_info(sock, if_name, &ifindex, &hwaddr);
  while (memcmp(frame.hdr.h_dest, hwaddr.sa_data, sizeof(frame.hdr.h_dest)) !=
         0) {
    recvfrom(sock, &frame, sizeof(frame), 0, NULL, NULL);
  }
  printf("got default entry: %d ", frame.default_entry);
  printf("from: ");
  print_mac(frame.hdr.h_source);
  printf("\n");
}

void listen_default(int sock, char *if_name) {
  struct RequestFrame frame;
  while (memcmp(frame.hdr.h_dest, ether_broadcast_addr,
                sizeof(ether_broadcast_addr)) != 0) {
    recvfrom(sock, &frame, sizeof(frame), 0, NULL, NULL);
  }
  printf("received request from: ");
  print_mac(frame.hdr.h_source);
  printf("\n");
  bool sent = false;
  for (int i = 0; i < remote_default_entries.size; ++i) {
    struct RemoteDefaultData *data = &remote_default_entries.data[i];
    // find the correct mac address data in the vector
    if (data->mac.x == ((struct uint48 *)frame.hdr.h_source)->x) {
      // sent the default entry packet for the mac address
      send_packet(sock, if_name, &data->mac, &data->default_entry,
                  sizeof(data->default_entry));
      printf("sent default: %d", data->default_entry);
      sent = true;
      break;
    }
  }
  if (!sent) {
    printf("failed to find entry for mac address: ");
    print_mac(frame.hdr.h_source);
    printf("\n");
    printf("entries: \n");
    for (int i = 0; i < remote_default_entries.size; ++i) {
      struct RemoteDefaultData *data = &remote_default_entries.data[i];
      print_mac(&data->mac);
      printf(" %x \n", data->default_entry);
    }
    exit(1);
  }
}

void load_defaults(char *filename) {
  // FF:FF:FF:FF:FF:FF 1\n is 20 characters
  const unsigned int line_size = 20;
  struct stat file_status;
  if (stat(filename, &file_status) < 0) {
    printf("%s", strerror(errno));
    exit(errno);
  }

  FILE *f;
  f = fopen(filename, "r");
  char file_buffer[file_status.st_size];
  fgets(file_buffer, file_status.st_size, f);

  remote_default_entries.size = file_status.st_size / line_size;
  remote_default_entries.data =
      malloc(sizeof(struct RemoteDefaultData) * remote_default_entries.size);
  memset(remote_default_entries.data, 0,
         sizeof(struct RemoteDefaultData) * remote_default_entries.size);

  for (int i = 0; i < remote_default_entries.size; ++i) {
    unsigned char *mac = (unsigned char *)&remote_default_entries.data[i].mac;
    sscanf(&file_buffer[i * line_size],
           "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx %hhx\n", &mac[0], &mac[1],
           &mac[2], &mac[3], &mac[4], &mac[5],
           &remote_default_entries.data[i].default_entry);
  }
}

int main(int argc, char *argv[]) {
  int sock = setup_socket();
  char *if_name = argv[1];
  if (strcmp(argv[2], "request") == 0) {
    printf("sending request \n");
    request_default(sock, if_name);
  } else if (strcmp(argv[2], "listen") == 0) {
    printf("listening for request \n");
    load_defaults("config");
    listen_default(sock, if_name);
  } else {
    printf("valid usage: ./remote_default_util interface_name request\n");
    printf("valid usage: ./remote_default_util interface_name listen [[mac] "
           "[default_entry]]...");
  }
  close(sock);
}
