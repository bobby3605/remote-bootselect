#include "remote-bootselect-server.h"
#include "util.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

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

int setup_config_fifo() {
  const char folder_path[] = "/tmp/remote-bootselect-server";
  const char config_path[] = "/tmp/remote-bootselect-server/config";
  umask(0000);
  mkdir(folder_path, 0777);
  mkfifo(config_path, 0777);
  return open(config_path, O_RDONLY);
}

int setup_listen_socket() {
  int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
  if (sock == -1) {
    printf("failed to setup listen socket: %s\n", strerror(errno));
    exit(errno);
  }
  drain_socket(sock);
  if (setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter)) !=
      0) {
    printf("failed to attach listen socket filter: %s\n", strerror(errno));
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
    printf("bad length when getting interface info: %s\n", strerror(errno));
    exit(errno);
  }

  if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
    printf("failed to get interface index: %s\n", strerror(errno));
    exit(errno);
  }
  *index = ifr.ifr_ifindex;

  if (ioctl(sock, SIOCGIFHWADDR, &ifr) == -1) {
    printf("failed to get interface mac address: %s\n", strerror(errno));
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
    printf("failed to send packet: %s\n", strerror(errno));
    free(frame);
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
  printf("got default entry: %s ", frame.default_entry);
  printf("from: ");
  print_mac(frame.hdr.h_source);
  printf("\n");
}

void setup_epoll(int epfd, int listen_socket, int config_fifo) {
  struct epoll_event event;
  event.events = EPOLLIN;
  event.data = (epoll_data_t)LISTEN_SOCKET;
  epoll_ctl(epfd, EPOLL_CTL_ADD, listen_socket, &event);
  event.data = (epoll_data_t)CONFIG_FIFO;
  epoll_ctl(epfd, EPOLL_CTL_ADD, config_fifo, &event);
}

struct RemoteDefaultData *find_default_data(struct uint48 source_mac) {
  for (int i = 0; i < remote_default_entries.size; ++i) {
    struct RemoteDefaultData *data = &remote_default_entries.data[i];
    if (data->mac.x == source_mac.x) {
      return data;
    }
  }
  return NULL;
}

void process_listen_socket(int listen_socket, char *if_name) {
  struct RequestFrame frame = {0};
  recv(listen_socket, &frame, sizeof(frame), 0);
  // check that it is a broadcast packet
  if (memcmp(frame.hdr.h_dest, ether_broadcast_addr,
             sizeof(ether_broadcast_addr)) != 0) {
    return;
  }

  printf("received request from: ");
  print_mac(frame.hdr.h_source);
  printf("\n");

  struct RemoteDefaultData *data =
      find_default_data(*(struct uint48 *)frame.hdr.h_source);
  if (data) {
    send_packet(listen_socket, if_name, &data->mac, &data->default_entry,
                sizeof(data->default_entry));
    printf("sent default:%s\n", data->default_entry);
  } else {
    printf("failed to find entry for mac address: ");
    print_mac(frame.hdr.h_source);
    printf("\n");
    print_entries();
  }
}

void set_default_data(struct RemoteDefaultData *default_data) {
  for (unsigned int i = 0; i < remote_default_entries.size; ++i) {
    if (remote_default_entries.data[i].mac.x == default_data->mac.x) {
      memset(remote_default_entries.data[i].default_entry, 0,
             sizeof(remote_default_entries.data[i].default_entry));
      strcpy(remote_default_entries.data[i].default_entry,
             default_data->default_entry);
      return;
    }
  }
  // If the loop didn't return, then the default data doesn't exist in the
  // buffer. Increase the size of the buffer and add the data.
  struct RemoteDefaultData *new_data =
      malloc(sizeof(struct RemoteDefaultData) * ++remote_default_entries.size);
  memcpy(new_data, remote_default_entries.data,
         sizeof(struct RemoteDefaultData) * (remote_default_entries.size - 1));
  free(remote_default_entries.data);
  remote_default_entries.data = new_data;
  memcpy(&remote_default_entries.data[remote_default_entries.size - 1],
         default_data, sizeof(struct RemoteDefaultData));
}

void process_config_fd(int config_fd) {
  size_t bufsize = 0;
  if (ioctl(config_fd, FIONREAD, &bufsize) < 0) {
    printf("failed to get buffer size for config socket: %s\n",
           strerror(errno));
    return;
  }
  char *config_buffer = malloc(bufsize);
  if (config_buffer == NULL) {
    printf("null config read buffer\n");
    exit(1);
  }
  int config_size = read(config_fd, config_buffer, bufsize);
  if (config_size == -1) {
    printf("config recv failed: %s\n", strerror(errno));
    exit(errno);
  }
  for (int i = 0; i < config_size;) {
    struct RemoteDefaultData default_data = {0};
    unsigned char *mac = (unsigned char *)&default_data.mac;
    sscanf(&config_buffer[i], "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx %s\n",
           &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5],
           default_data.default_entry);
    set_default_data(&default_data);
    i += 18; // MAC with : is 18 characters
    i += strlen(default_data.default_entry);
    i += 1; // 1 more for the \n
  }
  free(config_buffer);
}

void listen_default(int listen_socket, char *if_name) {
  int config_fifo = setup_config_fifo();
  int epfd = epoll_create1(0);
  setup_epoll(epfd, listen_socket, config_fifo);
  struct epoll_event events[2];
  while (true) {
    int event_count = epoll_wait(epfd, events, sizeof(events), -1);
    if (event_count > 0) {
      for (int i = 0; i < event_count; ++i) {
        // ignore EPOLLHUP
        // this is needed because read() on a pipe with no other end causes
        // EPOLLHUP without this check, there is an infinite loop here
        if (events[i].events != EPOLLHUP) {
          switch (events[i].data.u32) {
          case LISTEN_SOCKET:
            printf("LISTEN_SOCKET\n");
            process_listen_socket(listen_socket, if_name);
            break;
          case CONFIG_FIFO:
            printf("CONFIG_SOCKET\n");
            process_config_fd(config_fifo);
            break;
          }
        }
      }
    } else {
      printf("epoll_wait count <= 0: %s\n", strerror(errno));
      break;
    }
  }
  close(config_fifo);
  close(epfd);
}

int main(int argc, char *argv[]) {
  int sock = setup_listen_socket();
  // drop root permissions
  drop_permissions();
  int curr_arg = 0;
  char *if_name;
  while (curr_arg < argc) {
    ++curr_arg;
    if (strcmp(argv[curr_arg], "-i") == 0) {
      if_name = argv[++curr_arg];
    } else if (strcmp(argv[curr_arg], "-c") == 0) {
      // load config file
      int f = open(argv[++curr_arg], O_RDONLY);
      process_config_fd(f);
      close(f);
    } else if (strcmp(argv[curr_arg], "-r") == 0) {
      printf("sending request \n");
      request_default(sock, if_name);
      break;
    } else if (strcmp(argv[curr_arg], "-l") == 0) {
      printf("listening for request \n");
      listen_default(sock, if_name);
      break;
    } else {
      printf("valid usage: ./remote_default_util -i interface_name [-c "
             "config_file] -l/r (listen or request)\n");
      break;
    }
  }
  close(sock);
}
