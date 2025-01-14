#include <grub/dl.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/net.h>
#include <grub/net/ethernet.h>
#include <grub/time.h>
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
    grub_printf(
        "Failed to find any network cards. Did you call insmod efinet ?\n");
    return 1;
  }

  grub_err_t err;
  struct grub_net_card *card = &grub_net_cards[0];

  if (!card->opened) {
    err = GRUB_ERR_NONE;
    if (card->driver->open)
      err = card->driver->open(card);
    if (err)
      return err;
    card->opened = 1;
  }

  const grub_uint16_t ethertype = grub_cpu_to_be16(BOOTSELECT_ETHERTYPE);
  const grub_uint8_t ether_broadcast_addr[] = {0xff, 0xff, 0xff,
                                               0xff, 0xff, 0xff};

  // create frame
  struct RequestFrame request = {0};
  grub_memcpy(request.hdr.dst, ether_broadcast_addr, 6);
  grub_memcpy(request.hdr.src, &card->default_address.mac, 6);
  request.hdr.type = ethertype;

  // load frame into grub_net_buff
  struct grub_net_buff *request_nb = grub_netbuff_alloc(sizeof(request));
  grub_netbuff_reserve(request_nb, sizeof(request));
  grub_netbuff_push(request_nb, sizeof(request));
  grub_memcpy(request_nb->data, &request, sizeof(request));

  // send frame
  card->driver->send(card, request_nb);

  grub_netbuff_free(request_nb);

  struct grub_net_buff *response;
  struct etherhdr hdr = {0};
  const grub_uint64_t timeout_ms = 2000;
  grub_uint64_t limit_time = grub_get_time_ms() + timeout_ms;
  do {
    response = card->driver->recv(card);
    hdr = *(struct etherhdr *)response->data;
    if (limit_time < grub_get_time_ms()) {
      grub_printf("timeout waiting for response\n");
      break;
    }
  } while (hdr.type != ethertype);
  if (hdr.type == ethertype) {
    grub_netbuff_pull(response, sizeof(struct etherhdr));
    grub_printf("received default int:%hhx\n",
                *(grub_uint8_t *)&response->data);
    //    grub_env_set("default", *(grub_uint8_t *)&response->data);
    grub_env_set("default", "1");
    grub_int32_t buf_size =
        grub_snprintf(NULL, 0, "%hhx", *(grub_uint8_t *)&response->data);
    if (buf_size > 0) {
      // + 1 because snprintf doesn't count the null character
      char *default_str = grub_malloc(buf_size + 1);
      grub_snprintf(default_str, buf_size + 1, "%hhx",
                    *(grub_uint8_t *)&response->data);
      grub_printf("received default str:%s\n", default_str);
      grub_env_set("default", default_str);
      grub_free(default_str);
    } else {
      if (buf_size < 0) {
        grub_printf("snprintf error: %d", buf_size);
      }
      return 1;
    }
    return 0;
  } else {
    return 1;
  }
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(remote_bootselect) {
  cmd = grub_register_extcmd(
      "remote_bootselect", grub_cmd_remote_bootselect, 0, 0,
      N_("Get the default boot option from the network."), 0);
}

GRUB_MOD_FINI(remote_bootselect) { grub_unregister_extcmd(cmd); }
