#include <grub/dl.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/net.h>
#include <grub/net/ethernet.h>
#include <grub/types.h>

#define BOOTSELECT_ETHERTYPE 0x7184

struct etherhdr {
  grub_uint8_t dst[6];
  grub_uint8_t src[6];
  grub_uint16_t type;
} GRUB_PACKED;

struct RequestFrame {
  struct etherhdr hdr;
};

struct DataFrame {
  struct etherhdr hdr;
  unsigned char default_entry;
};

GRUB_MOD_LICENSE("GPLv3+");

static grub_err_t grub_cmd_remote_bootselect(grub_extcmd_context_t cmd
                                             __attribute__((unused)),
                                             int argc __attribute__((unused)),
                                             char **args
                                             __attribute__((unused))) {
  if (grub_net_cards == NULL) {
    grub_printf("failed to find any network cards\n");
    return 1;
  }
  grub_printf("found cards\n");
  /*
  grub_printf("found cards\n");
  struct grub_net_card *card = &grub_net_cards[0];
  grub_printf("alloc netbuff\n");
  struct grub_net_buff *nb = grub_netbuff_alloc(sizeof(struct RequestFrame));
  struct RequestFrame *request = (struct RequestFrame *)nb;
  const grub_uint8_t ether_broadcast_addr[] = {0xff, 0xff, 0xff,
                                               0xff, 0xff, 0xff};
  grub_printf("memcpy\n");
  grub_memcpy(request->hdr.dst, ether_broadcast_addr, 6);
  grub_memcpy(request->hdr.src, &card->default_address, 6);
  grub_printf("type\n");
  request->hdr.type = grub_cpu_to_be16(BOOTSELECT_ETHERTYPE);
  */

  grub_printf("alloc netbuff\n");
  struct grub_net_buff nb;
  grub_uint8_t packet_data[16];

  /* Build a request packet.  */
  nb.head = packet_data;
  nb.end = packet_data + sizeof(packet_data);
  grub_netbuff_clear(&nb);
  grub_netbuff_reserve(&nb, 16);

  grub_err_t err = grub_netbuff_push(&nb, sizeof(*packet_data));
  if (err)
    return err;

  grub_printf("build params\n");
  grub_net_link_level_address_t target_mac_addr;
  target_mac_addr.mac[0] = 0xff;
  target_mac_addr.mac[1] = 0xff;
  target_mac_addr.mac[2] = 0xff;
  target_mac_addr.mac[3] = 0xff;
  target_mac_addr.mac[4] = 0xff;
  target_mac_addr.mac[5] = 0xff;
  struct grub_net_network_level_interface inf;
  inf.card = &grub_net_cards[0];
  inf.vlantag = 0;
  inf.name = grub_xasprintf("%s:bootselect", inf.card->name);
  grub_memcpy(&inf.hwaddress, &inf.card->default_address,
              sizeof(inf.hwaddress));
  grub_printf("send packet\n");
  err = send_ethernet_packet(&inf, &nb, target_mac_addr, BOOTSELECT_ETHERTYPE);
  if (err != 0) {
    grub_printf("error sending packet\n");
    return err;
  }
  grub_printf("sent packet\n");

  struct grub_net_buff *response;
  struct etherhdr hdr = {0};
  grub_printf("waiting for response\n");
  do {
    response = inf.card->driver->recv(inf.card);
    hdr = *(struct etherhdr *)response->data;
    // grub_printf("received ethertype: %hx\n", hdr.type);
    //   fflush(NULL);
  } while (hdr.type != grub_cpu_to_be16(BOOTSELECT_ETHERTYPE));
  grub_netbuff_pull(response, sizeof(struct etherhdr));
  grub_uint8_t default_entry = *(grub_uint8_t *)response->data;
  grub_printf("received ethertype: %hx\n", hdr.type);
  grub_printf("received default: %u\n", default_entry);
  return 0;
  /* somehow this is an infinite loop
  grub_printf("printing cards\n");
  FOR_NET_CARDS(card) { grub_printf("card: %s \n", card->name); }
  */

  /*
  grub_printf("card %s opening\n", card->name);
  grub_err_t err;
  if (!card->opened) {
    err = GRUB_ERR_NONE;
    if (card->driver->open)
      err = card->driver->open(card);
    if (err) {
      grub_printf("error opening card: %x\n", err);
      return err;
    }
    card->opened = 1;
  }
  grub_printf("card %s opened\n", card->name);
  err = card->driver->send(card, nb);
  if (err) {
    grub_printf("error sending packet: %x", err);
    return err;
  } else {
    grub_printf("data sent\n");
    return 0;
  }
  return 1;
  */
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(remote_bootselect) {
  cmd = grub_register_extcmd(
      "remote_bootselect", grub_cmd_remote_bootselect, 0, 0,
      N_("Get the default boot option from the network."), 0);
}

GRUB_MOD_FINI(remote_bootselect) { grub_unregister_extcmd(cmd); }
