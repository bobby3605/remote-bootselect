#include "util.h"
#include "common.h"
#include <errno.h>
#include <linux/filter.h>
#include <netpacket/packet.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

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

void drop_permissions() {
  struct passwd *user = getpwnam("remote-bootselect");
  if (user == NULL) {
    printf("failed to get remote-bootselect user: %s", strerror(errno));
    exit(errno);
  }
  setuid(user->pw_uid);
  setgid(user->pw_gid);
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

void print_entries() {
  printf("entries:\n");
  for (int i = 0; i < remote_default_entries.size; ++i) {
    struct RemoteDefaultData *data = &remote_default_entries.data[i];
    print_mac(&data->mac);
    printf(" %s\n", data->default_entry);
  }
}
