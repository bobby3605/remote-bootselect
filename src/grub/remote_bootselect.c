#include <grub/dl.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/menu.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/net.h>
#include <grub/net/ethernet.h>
#include <grub/time.h>
#include <grub/types.h>

#define atoi_1(p) (*(p) - '0')

#define BOOTSELECT_ETHERTYPE 0x7184

struct __attribute__((packed)) etherhdr {
    grub_uint8_t dst[6];
    grub_uint8_t src[6];
    grub_uint16_t type;
};

static const grub_uint8_t ether_broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// NOTE: This module is AGPL3, however grub currently only checks for GPL3
// TODO: File a bug report with grub and/or verify that an AGPL3 license is
// compatible
GRUB_MOD_LICENSE("GPLv3+");

static struct grub_net_card *get_card(int card_idx) {
    grub_dl_load("efinet");
    if (grub_net_cards == NULL) {
        grub_printf("Failed to find any network cards.\n");
        return NULL;
    }

    struct grub_net_card *card;
    int i = 0;
    bool found_card = false;
    FOR_NET_CARDS(card) {
        if (i == card_idx) {
            found_card = true;
            break;
        } else {
            ++i;
        }
    }
    if (!found_card) {
        grub_printf("Failed to find network card: %d\n", card_idx);
        return NULL;
    }

    grub_err_t err;
    if (!card->opened) {
        err = GRUB_ERR_NONE;
        if (card->driver->open)
            err = card->driver->open(card);
        if (err)
            return NULL;
        card->opened = 1;
    }
    return card;
}

static grub_err_t netbuff_append(struct grub_net_buff *nb, const void *data, grub_size_t size) {
    grub_err_t err = grub_netbuff_put(nb, size);
    if (err == GRUB_ERR_NONE) {
        grub_memcpy(nb->tail - size, data, size);
    }
    return err;
}

static void *netbuff_get(struct grub_net_buff *nb, grub_size_t size) {
    void *data = nb->data;
    grub_err_t err = grub_netbuff_pull(nb, size);
    if (err != GRUB_ERR_NONE) {
        return NULL;
    } else {
        return data;
    }
}

static grub_err_t append_etherhdr(struct grub_net_buff *nb, struct grub_net_card *card) {
    grub_uint16_t ethertype = grub_cpu_to_be16(BOOTSELECT_ETHERTYPE);
    struct etherhdr hdr;
    grub_memcpy(hdr.dst, ether_broadcast_addr, 6);
    grub_memcpy(hdr.src, &card->default_address.mac, 6);
    hdr.type = ethertype;
    return netbuff_append(nb, &hdr, sizeof(hdr));
}

static void flush_recv(struct grub_net_card *card) {
    struct grub_net_buff *flush;
    while ((flush = card->driver->recv(card)) != NULL) {
        grub_netbuff_free(flush);
    }
}

static grub_err_t grub_cmd_remote_bootselect(grub_extcmd_context_t cmd __attribute__((unused)), int argc, char **args) {
    grub_uint16_t ethertype = grub_cpu_to_be16(BOOTSELECT_ETHERTYPE);

    int card_idx = argc > 0 ? atoi_1(args[0]) : 0;

    struct grub_net_card *card = get_card(card_idx);
    if (card == NULL) {
        grub_printf("failed to get card\n");
        return 1;
    }

    struct grub_net_buff *nb = grub_netbuff_alloc(sizeof(struct etherhdr));

    grub_err_t err = append_etherhdr(nb, card);
    if (err != GRUB_ERR_NONE) {
        grub_netbuff_free(nb);
        return err;
    }

    flush_recv(card);

    const grub_uint64_t timeout_ms = 1000;
    grub_uint64_t limit_time = grub_get_time_ms() + timeout_ms;
    while (grub_get_time_ms() < limit_time) {
        card->driver->send(card, nb);
        grub_millisleep(10);
        struct grub_net_buff *response = card->driver->recv(card);
        if (response) {
            struct etherhdr response_hdr;
            void *data = netbuff_get(response, sizeof(response_hdr));
            if (data == NULL) {
                grub_netbuff_free(response);
                continue;
            }
            response_hdr = *(struct etherhdr *)data;
            if (response_hdr.type == ethertype && (grub_memcmp(response_hdr.dst, &card->default_address.mac, 6) == 0)) {
                grub_uint8_t len;
                data = netbuff_get(response, sizeof(len));
                if (data == NULL) {
                    grub_printf("warning: expected length\n");
                    grub_netbuff_free(response);
                    continue;
                }
                len = *(grub_uint8_t *)data;
                data = netbuff_get(response, len);
                if (data == NULL) {
                    grub_printf("warning: expected str of size %d\n", len);
                    grub_netbuff_free(response);
                    continue;
                }
                char *entry = (char *)grub_malloc(len + 1);
                grub_memcpy(entry, data, len);
                entry[len] = '\0';
                grub_printf("got default:%s\n", entry);
                grub_env_set("default", entry);
                grub_free(entry);
                grub_netbuff_free(response);
                grub_netbuff_free(nb);
                return GRUB_ERR_NONE;
            }
            grub_netbuff_free(response);
        }
    }
    grub_printf("timeout waiting for response\n");
    grub_netbuff_free(nb);
    return 1;
}

static grub_err_t grub_cmd_remote_bootselect_export(grub_extcmd_context_t cmd __attribute__((unused)), int argc, char **args) {
    grub_menu_t grub_menu = grub_env_get_menu();
    if (grub_menu == NULL) {
        grub_printf("failed to get menu\n");
        return 1;
    }

    int card_idx = argc > 0 ? atoi_1(args[0]) : 0;

    struct grub_net_card *card = get_card(card_idx);
    if (card == NULL) {
        grub_printf("failed to get card\n");
        return 1;
    }

    grub_uint64_t totalSize = 0;
    for (grub_menu_entry_t entry = grub_menu->entry_list; entry != NULL; entry = entry->next) {
        if (!entry->id || !entry->title || entry->submenu) {
            continue;
        }
        // + 1 to include \0
        grub_uint32_t id_len = grub_strlen(entry->id) + 1;
        grub_uint32_t title_len = grub_strlen(entry->title) + 1;
        totalSize += id_len + title_len;
    }

    struct grub_net_buff *nb = grub_netbuff_alloc(sizeof(struct etherhdr) + totalSize);

    grub_err_t err = append_etherhdr(nb, card);
    if (err != GRUB_ERR_NONE) {
        grub_netbuff_free(nb);
        return err;
    }

    for (grub_menu_entry_t entry = grub_menu->entry_list; entry != NULL; entry = entry->next) {
        if (!entry->id || !entry->title) {
            grub_printf("warning: missing id or title on a menuentry\n");
            continue;
        }
        if (entry->submenu) {
            // TODO:
            // support submenus
            continue;
        }
        // + 1 to include \0
        grub_uint32_t id_len = grub_strlen(entry->id) + 1;
        netbuff_append(nb, entry->id, id_len);

        grub_uint32_t title_len = grub_strlen(entry->title) + 1;
        netbuff_append(nb, entry->title, title_len);
    }

    card->driver->send(card, nb);
    grub_netbuff_free(nb);
    return 0;
}

static grub_extcmd_t remote_bootselect_cmd;
static grub_extcmd_t remote_bootselect_export;

GRUB_MOD_INIT(remote_bootselect) {
    remote_bootselect_cmd =
        grub_register_extcmd("remote_bootselect", grub_cmd_remote_bootselect, 0, 0, N_("Get the default boot option from the network."), 0);
    remote_bootselect_export =
        grub_register_extcmd("remote_bootselect_export", grub_cmd_remote_bootselect_export, 0, 0, N_("Send menu entries to network."), 0);
}

GRUB_MOD_FINI(remote_bootselect) {
    grub_unregister_extcmd(remote_bootselect_cmd);
    grub_unregister_extcmd(remote_bootselect_export);
}
