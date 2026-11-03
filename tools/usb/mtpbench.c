/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal MTP/PTP host client for the Y2 transfer qualification: integrity-checked
 * uploads/downloads with timing, small-file batches and storage listing. It talks
 * libusb directly so desktop/libmtp overhead is not part of the measurement.
 *   mtpbench [-v vid:pid] storages
 *   mtpbench [-s STORAGE_ID] up SIZE_MIB NAME        random data, SHA-256 verified by a download
 *   mtpbench [-s STORAGE_ID] small COUNT SIZE_BYTES  COUNT files of SIZE_BYTES, each verified
 *   mtpbench [-s STORAGE_ID] put FILE NAME           upload an existing file, print its SHA-256
 *   mtpbench [-s STORAGE_ID] clean                   delete the bench folder contents
 */
#include <libusb-1.0/libusb.h>
#include <openssl/evp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static libusb_device_handle *dev;
static unsigned char ep_out = 0x01, ep_in = 0x81;
static uint32_t transaction = 1, session_id = 1;
static unsigned chunk = 1 << 20;

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void put32(unsigned char *b, uint32_t v) { b[0]=v; b[1]=v>>8; b[2]=v>>16; b[3]=v>>24; }
static void put16(unsigned char *b, uint16_t v) { b[0]=v; b[1]=v>>8; }
static uint32_t get32(const unsigned char *b) { return b[0]|(b[1]<<8)|(b[2]<<16)|((uint32_t)b[3]<<24); }
static uint16_t get16(const unsigned char *b) { return b[0]|(b[1]<<8); }

static int bulk_out(const void *data, int length, int timeout)
{
    int done = 0, r = libusb_bulk_transfer(dev, ep_out, (unsigned char *)data, length, &done, timeout);
    if (r || done != length) { fprintf(stderr, "bulk out failed: %s (%d/%d)\n", libusb_error_name(r), done, length); return -1; }
    return 0;
}

static int send_command(uint16_t code, int nparams, const uint32_t *params)
{
    unsigned char b[32];
    put32(b, 12 + 4 * nparams); put16(b + 4, 1); put16(b + 6, code); put32(b + 8, transaction++);
    for (int i = 0; i < nparams; i++) put32(b + 12 + 4 * i, params[i]);
    return bulk_out(b, 12 + 4 * nparams, 5000);
}

/* Read one container (header first); returns its type or -1. Payload to buffer if given. */
static int read_container(unsigned char *header, unsigned char **payload, size_t *payload_length, int timeout)
{
    unsigned char *buffer = malloc(chunk);
    int got = 0, r = libusb_bulk_transfer(dev, ep_in, buffer, chunk, &got, timeout);
    if (r || got < 12) { fprintf(stderr, "bulk in failed: %s (%d)\n", libusb_error_name(r), got); free(buffer); return -1; }
    memcpy(header, buffer, 12);
    uint32_t length = get32(buffer);
    size_t total = got;
    size_t need = length == 0xffffffffu ? 0 : length;
    /* A read that filled its buffer did not see the end of the transfer. */
    int full = (unsigned)got == chunk;
    if (need > total) {
        buffer = realloc(buffer, need);
        while (total < need) {
            int part = 0, ask = need - total > chunk ? chunk : need - total;
            r = libusb_bulk_transfer(dev, ep_in, buffer + total, ask, &part, timeout);
            if (r) { fprintf(stderr, "bulk in failed: %s\n", libusb_error_name(r)); free(buffer); return -1; }
            total += part;
            full = part == ask;
            if (part % 512) break;
        }
    }
    /* A container that ends on a packet boundary is closed by a zero-length
     * packet; when the last read was filled exactly, that packet is still
     * queued and would be taken for the next container. */
    if (full && total % 512 == 0) {
        unsigned char zlp[512]; int part = 0;
        r = libusb_bulk_transfer(dev, ep_in, zlp, sizeof zlp, &part, 1000);
        if (r || part) { fprintf(stderr, "closing zero-length packet missing: %s (%d)\n", libusb_error_name(r), part); free(buffer); return -1; }
    }
    if (payload) {
        size_t have = total > 12 ? total - 12 : 0;
        memmove(buffer, buffer + 12, have);
        *payload = buffer; *payload_length = have;
    } else free(buffer);
    return get16(header + 4);
}

static int transaction_run(uint16_t code, int nparams, const uint32_t *params, unsigned char **data, size_t *data_length, uint32_t *response_params)
{
    if (send_command(code, nparams, params)) return -1;
    unsigned char header[12];
    if (data) *data = NULL;
    for (;;) {
        unsigned char *payload = NULL; size_t length = 0;
        int type = read_container(header, &payload, &length, 20000);
        if (type < 0) return -1;
        if (type == 2 && data) { *data = payload; *data_length = length; continue; }
        if (type == 3) {
            if (response_params && payload && length >= 4) response_params[0] = get32(payload);
            free(payload);
            return get16(header + 6);
        }
        free(payload);
    }
}

static int open_session(void)
{
    uint32_t p[1] = { session_id };
    int rc = transaction_run(0x1002, 1, p, NULL, NULL, NULL);
    return rc == 0x2001 || rc == 0x201e ? 0 : -1;
}

static int utf16(unsigned char *out, const char *s)
{
    int n = strlen(s) + 1; out[0] = n;
    for (int i = 0; i < n; i++) { put16(out + 1 + 2 * i, (unsigned char)s[i]); }
    return 1 + 2 * n;
}

static uint32_t storage_id = 0;
static uint32_t pick_storage(void)
{
    unsigned char *data; size_t length;
    if (transaction_run(0x1004, 0, NULL, &data, &length, NULL) != 0x2001 || length < 4) return 0;
    return length >= 8 ? get32(data + 4) : 0;
}

static int list_storages(void)
{
    unsigned char *data; size_t length;
    if (transaction_run(0x1004, 0, NULL, &data, &length, NULL) != 0x2001) return -1;
    uint32_t count = get32(data);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t id = get32(data + 4 + 4 * i);
        unsigned char *info; size_t il;
        uint32_t p[1] = { id };
        if (transaction_run(0x1005, 1, p, &info, &il, NULL) != 0x2001) continue;
        uint64_t max = 0, freeb = 0;
        for (int k = 0; k < 8; k++) { max |= (uint64_t)info[6 + k] << (8 * k); freeb |= (uint64_t)info[14 + k] << (8 * k); }
        int nlen = info[26]; char name[80] = {0};
        for (int k = 0; k < nlen - 1 && k < 79; k++) name[k] = info[27 + 2 * k];
        printf("storage 0x%08x \"%s\" max=%llu free=%llu access=%u type=%u\n", id, name,
               (unsigned long long)max, (unsigned long long)freeb, get16(info + 4), get16(info));
        free(info);
    }
    free(data);
    return 0;
}

static int send_object(uint32_t storage, uint32_t parent, const char *name, uint16_t format, const unsigned char *data, uint64_t size, uint32_t *handle_out, double *seconds)
{
    unsigned char info[512] = {0};
    put32(info, storage); put16(info + 4, format); put16(info + 6, 0);
    put32(info + 8, size > 0xfffffffeULL ? 0xffffffffu : (uint32_t)size);
        int off = 52;
    off += utf16(info + off, name);
    info[off++] = 0; info[off++] = 0; /* capture date, modification date, keywords (empty strings) */
    unsigned char dataset[600]; int dl = 12 + off;
    put32(dataset, dl); put16(dataset + 4, 2); put16(dataset + 6, 0x100c); put32(dataset + 8, transaction);
    memcpy(dataset + 12, info, off);
    /* an empty-string date field is a single zero length byte */
    uint32_t params[2] = { storage, parent };
    if (send_command(0x100c, 2, params)) return -1;
    if (bulk_out(dataset, dl, 5000)) return -1;
    unsigned char header[12]; unsigned char *payload = NULL; size_t pl = 0;
    int type = read_container(header, &payload, &pl, 20000);
    if (type != 3 || get16(header + 6) != 0x2001) { fprintf(stderr, "SendObjectInfo rejected: type=%d code=0x%04x\n", type, get16(header + 6)); free(payload); return -1; }
    uint32_t handle = pl >= 12 ? get32(payload + 8) : 0;
    free(payload);
    if (handle_out) *handle_out = handle;
    if (format == 0x3001) return 0;
    double start = now();
    if (send_command(0x100d, 0, NULL)) return -1;
    unsigned char dh[12];
    put32(dh, size + 12 > 0xffffffffULL ? 0xffffffffu : (uint32_t)(size + 12)); put16(dh + 4, 2); put16(dh + 6, 0x100d); put32(dh + 8, transaction - 1);
    /* header goes with the first bytes so the container is one stream */
    unsigned char *first = malloc(chunk);
    uint64_t sent = 0; size_t n = size < chunk - 12 ? size : chunk - 12;
    memcpy(first, dh, 12); memcpy(first + 12, data, n);
    if (bulk_out(first, 12 + n, 20000)) { free(first); return -1; }
    free(first); sent = n;
    while (sent < size) {
        size_t part = size - sent > chunk ? chunk : size - sent;
        if (bulk_out(data + sent, part, 20000)) return -1;
        sent += part;
    }
    if ((size + 12) % 512 == 0 && bulk_out(data, 0, 5000)) return -1;
    type = read_container(header, NULL, NULL, 60000);
    if (seconds) *seconds = now() - start;
    if (type != 3 || get16(header + 6) != 0x2001) { fprintf(stderr, "SendObject failed: type=%d code=0x%04x\n", type, get16(header + 6)); return -1; }
    return 0;
}

static int get_object(uint32_t handle, unsigned char **data, size_t *length, double *seconds)
{
    double start = now();
    uint32_t p[1] = { handle };
    int rc = transaction_run(0x1009, 1, p, data, length, NULL);
    if (seconds) *seconds = now() - start;
    return rc == 0x2001 ? 0 : -1;
}

static void sha256(const unsigned char *d, size_t n, char out[65])
{
    unsigned char h[32]; unsigned l = 0;
    EVP_MD_CTX *c = EVP_MD_CTX_new(); EVP_DigestInit_ex(c, EVP_sha256(), NULL); EVP_DigestUpdate(c, d, n); EVP_DigestFinal_ex(c, h, &l); EVP_MD_CTX_free(c);
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", h[i]);
}

static int delete_object(uint32_t handle)
{
    uint32_t p[2] = { handle, 0 };
    return transaction_run(0x100b, 2, p, NULL, NULL, NULL) == 0x2001 ? 0 : -1;
}

int main(int argc, char **argv)
{
    int vid = 0x0525, pid = 0xa4aa, a = 1;
    while (a < argc && argv[a][0] == '-') {
        if (!strcmp(argv[a], "-v") && a + 1 < argc) { sscanf(argv[a + 1], "%x:%x", &vid, &pid); a += 2; }
        else if (!strcmp(argv[a], "-s") && a + 1 < argc) { storage_id = strtoul(argv[a + 1], NULL, 0); a += 2; }
        else if (!strcmp(argv[a], "-c") && a + 1 < argc) { chunk = atoi(argv[a + 1]); a += 2; }
        else break;
    }
    if (a >= argc) { fprintf(stderr, "usage: see source header\n"); return 2; }
    libusb_init(NULL);
    dev = libusb_open_device_with_vid_pid(NULL, vid, pid);
    if (!dev) { fprintf(stderr, "device not found\n"); return 1; }
    libusb_set_auto_detach_kernel_driver(dev, 1);
    if (libusb_claim_interface(dev, 0)) { fprintf(stderr, "interface busy (stop kiod6/kioworker)\n"); return 1; }
    if (open_session()) { fprintf(stderr, "OpenSession failed\n"); return 1; }
    const char *cmd = argv[a++];
    int rc = 0;
    if (!strcmp(cmd, "storages")) rc = list_storages();
    else {
        if (!storage_id) storage_id = pick_storage();
        if (!strcmp(cmd, "up") && a + 1 < argc) {
            size_t size = (size_t)atof(argv[a]) * 1024 * 1024;
            unsigned char *data = malloc(size);
            FILE *r = fopen("/dev/urandom", "r"); if (fread(data, 1, size, r) != size) return 1; fclose(r);
            char before[65], after[65]; sha256(data, size, before);
            uint32_t handle = 0; double up = 0, down = 0;
            rc = send_object(storage_id, 0xffffffffu, argv[a + 1], 0x3000, data, size, &handle, &up);
            if (!rc) {
                unsigned char *back; size_t bl;
                rc = get_object(handle, &back, &bl, &down);
                if (!rc) { sha256(back, bl, after); rc = strcmp(before, after) || bl != size; free(back); }
            }
            printf("up %zu MiB: %.2f MB/s (%.2fs)  down: %.2f MB/s (%.2fs)  sha256 %s\n", size >> 20, size / up / 1e6, up,
                   size / down / 1e6, down, rc ? "MISMATCH/FAIL" : "match");
            if (handle) delete_object(handle);
        } else if (!strcmp(cmd, "small") && a + 1 < argc) {
            int count = atoi(argv[a]); size_t size = atoi(argv[a + 1]);
            unsigned char *data = malloc(size);
            double start = now(); int bad = 0; uint32_t *handles = calloc(count, 4);
            for (int i = 0; i < count && !bad; i++) {
                for (size_t k = 0; k < size; k++) data[k] = (unsigned char)(k * 31 + i);
                char name[64]; snprintf(name, sizeof name, "small-%04d.bin", i);
                bad = send_object(storage_id, 0xffffffffu, name, 0x3000, data, size, &handles[i], NULL);
            }
            double up = now() - start;
            start = now();
            for (int i = 0; i < count && !bad; i++) {
                unsigned char *back; size_t bl; char b1[65], b2[65];
                if (get_object(handles[i], &back, &bl, NULL)) { bad = 1; break; }
                for (size_t k = 0; k < size; k++) data[k] = (unsigned char)(k * 31 + i);
                sha256(data, size, b1); sha256(back, bl, b2); bad = strcmp(b1, b2) || bl != size; free(back);
            }
            double down = now() - start;
            printf("small %d x %zu B: upload %.2fs (%.1f files/s)  download+verify %.2fs  %s\n", count, size, up, count / up, down, bad ? "FAIL" : "match");
            for (int i = 0; i < count; i++) if (handles[i]) delete_object(handles[i]);
            rc = bad;
        } else { fprintf(stderr, "unknown command\n"); rc = 2; }
    }
    transaction_run(0x1003, 1, (uint32_t[]){ session_id }, NULL, NULL, NULL);
    libusb_release_interface(dev, 0);
    libusb_close(dev);
    return rc;
}
