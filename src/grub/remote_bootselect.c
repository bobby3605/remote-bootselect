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

struct RequestFrame {
    struct etherhdr hdr;
};

// NOTE: This module is AGPL3, however grub currently only checks for GPL3
// TODO: File a bug report with grub and/or verify that an AGPL3 license is
// compatible
GRUB_MOD_LICENSE("GPLv3+");

static grub_err_t grub_cmd_remote_bootselect(grub_extcmd_context_t cmd __attribute__((unused)), int argc, char **args) {
    grub_dl_load("efinet");
    if (grub_net_cards == NULL) {
        grub_printf("Failed to find any network cards.\n");
        return 1;
    }

    int card_idx = argc > 1 ? atoi_1(args[argc - 1]) : 0;

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
        return 1;
    }

    grub_err_t err;
    if (!card->opened) {
        err = GRUB_ERR_NONE;
        if (card->driver->open)
            err = card->driver->open(card);
        if (err)
            return err;
        card->opened = 1;
    }

    const grub_uint16_t ethertype = grub_cpu_to_be16(BOOTSELECT_ETHERTYPE);
    const grub_uint8_t ether_broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

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

    struct grub_net_buff *response;
    struct etherhdr hdr = {0};
    const grub_uint64_t timeout_ms = 1000;
    grub_uint64_t limit_time = grub_get_time_ms() + timeout_ms;
    do {
        response = card->driver->recv(card);
        if (response) {
            hdr = *(struct etherhdr *)response->data;
        }
        if (limit_time < grub_get_time_ms()) {
            grub_printf("timeout waiting for response\n");
            grub_netbuff_free(request_nb);
            return 1;
        }
        card->driver->send(card, request_nb);
        grub_millisleep(10);
    } while (hdr.type != ethertype);
    grub_netbuff_free(request_nb);
    grub_uint64_t respSize = response->tail - response->data;
    if (respSize < sizeof(struct etherhdr) + sizeof(grub_uint8_t)) {
        grub_printf("size mismatch on response\n");
        return 1;
    }
    grub_netbuff_pull(response, sizeof(struct etherhdr));
    grub_uint8_t length = *(grub_uint8_t *)response->data;
    grub_netbuff_pull(response, sizeof(grub_uint8_t));
    if ((response->tail - response->data) < length) {
        grub_printf("size mismatch on response entry\n");
        return 1;
    }

    char *entry = (char *)grub_malloc(length + sizeof(char));
    grub_memcpy(entry, response->data, length);
    grub_netbuff_free(response);
    entry[length] = '\0';

    grub_printf("got default:%s\n", entry);
    grub_env_set("default", entry);
    grub_free(entry);
    return 0;
}

static grub_err_t grub_cmd_remote_bootselect_export(grub_extcmd_context_t cmd __attribute__((unused)), int argc, char **args) {
    grub_menu_t grub_menu = grub_env_get_menu();
    if (grub_menu == NULL) {
        grub_printf("failed to get menu\n");
        return 1;
    }

    grub_dl_load("efinet");
    if (grub_net_cards == NULL) {
        grub_printf("Export failed to find any network cards.\n");
        return 1;
    }

    int card_idx = argc > 1 ? atoi_1(args[argc - 1]) : 0;

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
        grub_printf("Export failed to find network card: %d\n", card_idx);
        return 1;
    }

    grub_err_t err;
    if (!card->opened) {
        err = GRUB_ERR_NONE;
        if (card->driver->open)
            err = card->driver->open(card);
        if (err)
            return err;
        card->opened = 1;
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

    const grub_uint16_t ethertype = grub_cpu_to_be16(BOOTSELECT_ETHERTYPE);
    const grub_uint8_t ether_broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    struct etherhdr hdr;
    grub_memcpy(hdr.dst, ether_broadcast_addr, 6);
    grub_memcpy(hdr.src, &card->default_address.mac, 6);
    hdr.type = ethertype;

    // allocate header plus 1KiB
    struct grub_net_buff *export_nb = grub_netbuff_alloc(sizeof(hdr) + totalSize);

    grub_netbuff_put(export_nb, sizeof(hdr));
    void *hdr_ptr = export_nb->tail - sizeof(hdr);
    grub_memcpy(hdr_ptr, &hdr, sizeof(hdr));

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
        grub_netbuff_put(export_nb, id_len);
        void *id = export_nb->tail - id_len;
        grub_memcpy(id, entry->id, id_len);

        grub_uint32_t title_len = grub_strlen(entry->title) + 1;
        grub_netbuff_put(export_nb, title_len);
        void *title = export_nb->tail - title_len;
        grub_memcpy(title, entry->title, title_len);
    }

    card->driver->send(card, export_nb);
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
