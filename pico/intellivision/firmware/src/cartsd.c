// See cartsd.h. Requests carry a sequence tag as their last param, which the
// reply echoes as its only param; the ROM push carries no params at all.

#include "cartsd.h"

#include <string.h>

#include "fujibus_usb.h"

// Debug builds put stdio on the same USB CDC link as these replies
#ifdef CARTSD_DEBUG
#include <stdio.h>
#define CARTSD_LOG(...) printf(__VA_ARGS__)
#else
#define CARTSD_LOG(...) ((void)0)
#endif

static uint8_t s_tag;

static void cartsd_reply(uint8_t command, const uint8_t *payload, size_t len)
{
    // Worst-case SLIP growth of fujibus_build_request()'s 384-byte packet
    static uint8_t buf[2 * 384 + 2];
    fb_param_t tag = { s_tag, 1 };
    size_t flen = fujibus_build_request(FUJI_DEVICEID_DBC, command, &tag, 1,
                                         payload, (uint16_t)len, buf, sizeof(buf));
    if (!flen) {
        CARTSD_LOG("cartsd: reply 0x%02x len=%u too big\n", command, (unsigned)len);
        return;
    }
    fujibus_send_frame(buf, flen);
}

static void cartsd_nak(void) { cartsd_reply(FUJICMD_NAK, NULL, 0); }

#if CONFIG_SD_STORAGE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "tusb.h"
#include "vfs.h"

// Names are kept full length so a listed name can be opened again; most of
// the RP2350's SRAM is cart.ROM[], hence the small entry cap.
#define CARTSD_MAX_ENTRIES 128
#define CARTSD_NAME_POOL   (12 * 1024)
#define CARTSD_MAX_PATH    256
#define CARTSD_MAX_NAME    255
// Keep in sync with READ_CHUNK in lib/device/Cartridge/CartSDClient.cpp
#define CARTSD_READ_CHUNK  256
// NET_OPEN stream ids 0 and 1 are the ROM push
#define CARTSD_STREAM_FILE 2

typedef struct {
    uint16_t name_off;
    bool isDir;
} cartsd_dirent_t;

static cartsd_dirent_t s_entries[CARTSD_MAX_ENTRIES];
static char s_names[CARTSD_NAME_POOL];
static size_t s_names_used;
static int s_entry_count;
static int s_entry_cursor;
static bool s_dir_open;

// Two slots: MediaTypeROM keeps the ROM open while it reads the .cfg sibling.
// A handle is (7-bit generation << 1) | slot, so one left over from a
// reclaimed slot is refused rather than reading the new file.
#define CARTSD_MAX_FILES 2
static vfs_file_t *s_files[CARTSD_MAX_FILES];
static uint8_t s_file_gen[CARTSD_MAX_FILES];
static uint32_t s_file_last_use[CARTSD_MAX_FILES];
static uint32_t s_use_clock;

#define CARTSD_HANDLE(slot) ((uint8_t)((s_file_gen[slot] << 1) | (slot)))

static void build_fullpath(char *out, size_t outsz, const uint8_t *path, size_t len)
{
    char rel[CARTSD_MAX_PATH] = "/";
    if (len > 0) {
        size_t n = len < sizeof(rel) - 1 ? len : sizeof(rel) - 1;
        memcpy(rel, path, n);
        rel[n] = '\0';
    }
    snprintf(out, outsz, "/sd%s", rel);
}

// Case-insensitive '*'/'?' glob, as util_wildcard_match() on the ESP32
static bool wildcard_match(const char *s, const char *p)
{
    const char *star_p = NULL, *star_s = NULL;

    while (*s) {
        if (*p == '?' || tolower((unsigned char)*p) == tolower((unsigned char)*s)) {
            s++;
            p++;
        } else if (*p == '*') {
            star_p = p++;
            star_s = s;
        } else if (star_p != NULL) {
            p = star_p + 1;
            s = ++star_s;
        } else {
            return false;
        }
    }
    while (*p == '*')
        p++;
    return *p == '\0';
}

static int entry_compare(const void *a, const void *b)
{
    const cartsd_dirent_t *ea = a, *eb = b;
    if (ea->isDir != eb->isDir)
        return ea->isDir ? -1 : 1;
    return strcasecmp(&s_names[ea->name_off], &s_names[eb->name_off]);
}

static void close_dir(void)
{
    s_dir_open = false;
    s_entry_count = 0;
    s_entry_cursor = 0;
    s_names_used = 0;
}

static void close_slot(int slot)
{
    if (s_files[slot] != NULL) {
        vfs_close(s_files[slot]);
        s_files[slot] = NULL;
    }
}

static int slot_for_handle(uint32_t handle)
{
    int slot = handle & 1;
    if (s_files[slot] == NULL || CARTSD_HANDLE(slot) != handle)
        return -1;
    return slot;
}

// A FILE_OPEN reply the ESP32 never saw, or an ESP32 reboot, leaks a slot,
// so reclaim the least recently used one rather than refusing forever.
static int alloc_slot(void)
{
    int lru = 0;
    for (int i = 0; i < CARTSD_MAX_FILES; i++) {
        if (s_files[i] == NULL)
            return i;
        if (s_file_last_use[i] < s_file_last_use[lru])
            lru = i;
    }
    close_slot(lru);
    return lru;
}

// Payload: "path\0" or "path\0pattern\0", as FUJI_OPEN_DIRECTORY on the ESP32.
// Read whole and sorted up front: vfs.c has no telldir/seekdir.
static void open_directory(const fb_reply_t *req)
{
    size_t pathlen = req->data_len ? strnlen((const char *)req->data, req->data_len) : 0;
    char pattern[64] = "";
    if (pathlen + 1 < req->data_len) {
        size_t restlen = req->data_len - (pathlen + 1);
        size_t n = restlen < sizeof(pattern) - 1 ? restlen : sizeof(pattern) - 1;
        memcpy(pattern, req->data + pathlen + 1, n);
        pattern[n] = '\0';
    }

    char fullpath[CARTSD_MAX_PATH + 4];
    build_fullpath(fullpath, sizeof(fullpath), req->data, pathlen);

    close_dir();
    vfs_dir_t *d = vfs_opendir(fullpath);
    if (!d) {
        cartsd_nak();
        return;
    }

    vfs_dirent_t ent;
    while (vfs_readdir(d, &ent) > 0) {
        tud_task(); // each entry is an SD transaction; keep USB serviced

        if (strcmp(ent.name, ".") == 0 || strcmp(ent.name, "..") == 0)
            continue;
        if (ent.type & (VFS_TYPE_HIDDEN | VFS_TYPE_SYSTEM))
            continue;

        bool isdir = (ent.type & VFS_TYPE_DIR) != 0;
        // Pattern filters files only, as FileSystemSDFAT::dir_open() does
        if (!isdir && pattern[0] != '\0' && !wildcard_match(ent.name, pattern))
            continue;

        // A truncated name would list but not open
        size_t namelen = strlen(ent.name);
        if (namelen > CARTSD_MAX_NAME)
            continue;
        if (s_entry_count >= CARTSD_MAX_ENTRIES ||
            s_names_used + namelen + 1 > sizeof(s_names)) {
            CARTSD_LOG("cartsd: \"%s\" listing truncated at %d\n", fullpath, s_entry_count);
            break;
        }

        cartsd_dirent_t *e = &s_entries[s_entry_count++];
        e->isDir = isdir;
        e->name_off = (uint16_t)s_names_used;
        memcpy(&s_names[s_names_used], ent.name, namelen + 1);
        s_names_used += namelen + 1;
    }
    vfs_closedir(d);

    qsort(s_entries, s_entry_count, sizeof(s_entries[0]), entry_compare);
    s_dir_open = true;
    cartsd_reply(FUJICMD_ACK, NULL, 0);
}

// Same reply as fujicmd_read_directory_entry(): '/' marks a directory,
// 0x7F 0x7F ends the listing.
static void read_dir_entry(void)
{
    if (!s_dir_open) {
        cartsd_nak();
        return;
    }
    if (s_entry_cursor >= s_entry_count) {
        static const uint8_t end[2] = { 0x7F, 0x7F };
        cartsd_reply(FUJICMD_ACK, end, sizeof(end));
        return;
    }

    const cartsd_dirent_t *e = &s_entries[s_entry_cursor++];
    uint8_t out[CARTSD_MAX_NAME + 2];
    size_t len = strlen(&s_names[e->name_off]);
    memcpy(out, &s_names[e->name_off], len);
    if (e->isDir)
        out[len++] = '/';
    out[len++] = '\0';
    cartsd_reply(FUJICMD_ACK, out, len);
}

static void get_directory_position(void)
{
    if (!s_dir_open) {
        cartsd_nak();
        return;
    }
    uint8_t out[2] = { (uint8_t)s_entry_cursor, (uint8_t)(s_entry_cursor >> 8) };
    cartsd_reply(FUJICMD_ACK, out, sizeof(out));
}

static void set_directory_position(const fb_reply_t *req)
{
    if (!s_dir_open || req->nparams < 2 || req->params[0] > (uint32_t)s_entry_count) {
        cartsd_nak();
        return;
    }
    s_entry_cursor = (int)req->params[0];
    cartsd_reply(FUJICMD_ACK, NULL, 0);
}

// Payload: stream id CARTSD_STREAM_FILE, then the path. Reply: handle, size LE.
static void open_file(const fb_reply_t *req)
{
    if (req->data_len < 1 || req->data[0] != CARTSD_STREAM_FILE) {
        cartsd_nak();
        return;
    }

    char fullpath[CARTSD_MAX_PATH + 4];
    build_fullpath(fullpath, sizeof(fullpath), req->data + 1, req->data_len - 1);

    vfs_stat_t st;
    if (vfs_stat(fullpath, &st) != 0 || (st.type & VFS_TYPE_DIR)) {
        cartsd_nak();
        return;
    }

    int slot = alloc_slot();
    s_files[slot] = vfs_open(fullpath, "r");
    if (!s_files[slot]) {
        cartsd_nak();
        return;
    }
    s_file_gen[slot] = (s_file_gen[slot] + 1) & 0x7F;
    s_file_last_use[slot] = ++s_use_clock;

    uint32_t size = (uint32_t)st.size;
    uint8_t out[5] = { CARTSD_HANDLE(slot), (uint8_t)size, (uint8_t)(size >> 8),
                       (uint8_t)(size >> 16), (uint8_t)(size >> 24) };
    CARTSD_LOG("cartsd: open \"%s\" -> %u, %lu bytes\n", fullpath, out[0], (unsigned long)size);
    cartsd_reply(FUJICMD_ACK, out, sizeof(out));
}

// Params: handle, offset, length, tag
static void read_file(const fb_reply_t *req)
{
    int slot = req->nparams >= 4 ? slot_for_handle(req->params[0]) : -1;
    if (slot < 0) {
        cartsd_nak();
        return;
    }
    s_file_last_use[slot] = ++s_use_clock;

    uint32_t want = req->params[2];
    if (want > CARTSD_READ_CHUNK)
        want = CARTSD_READ_CHUNK;

    static uint8_t buf[CARTSD_READ_CHUNK];
    int got = -1;
    if (vfs_lseek(s_files[slot], req->params[1]) == 0)
        got = vfs_read(s_files[slot], buf, want);
    if (got < 0) {
        cartsd_nak();
        return;
    }
    cartsd_reply(FUJICMD_ACK, buf, (size_t)got);
}

// Params: handle, tag. A stale handle closes nothing.
static void close_file(const fb_reply_t *req)
{
    int slot = req->nparams >= 2 ? slot_for_handle(req->params[0]) : -1;
    if (slot >= 0)
        close_slot(slot);
    cartsd_reply(FUJICMD_ACK, NULL, 0);
}

static void cartsd_dispatch(const fb_reply_t *req)
{
    switch (req->command) {
    case FUJICMD_OPEN_DIRECTORY:         open_directory(req);         break;
    case FUJICMD_READ_DIR_ENTRY:         read_dir_entry();            break;
    case FUJICMD_CLOSE_DIRECTORY:
        close_dir();
        cartsd_reply(FUJICMD_ACK, NULL, 0);
        break;
    case FUJICMD_GET_DIRECTORY_POSITION: get_directory_position();    break;
    case FUJICMD_SET_DIRECTORY_POSITION: set_directory_position(req); break;
    case NETCMD_OPEN:                    open_file(req);              break;
    case NETCMD_READ:                    read_file(req);              break;
    case NETCMD_CLOSE:                   close_file(req);             break;
    default:                             cartsd_nak();                break;
    }
}

#else /* !CONFIG_SD_STORAGE */

static void cartsd_dispatch(const fb_reply_t *req)
{
    (void)req;
    cartsd_nak();
}

#endif

bool cartsd_inbound_handler(const fb_reply_t *req)
{
    if (req->device != FUJI_DEVICEID_DBC || req->nparams == 0)
        return false;

    s_tag = (uint8_t)req->params[req->nparams - 1];
    cartsd_dispatch(req);
    return true;
}
