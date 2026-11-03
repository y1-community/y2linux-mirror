/* SPDX-License-Identifier: GPL-2.0-only */
/* USB file operations against a connected Y2 through libmtp, the library that
 * Linux file managers and macOS MTP apps use (Windows Explorer sends the same
 * MTP operations). Every step works inside one new folder on Internal
 * Storage (and the SD card when present) and ends by checking the player
 * still answers. Covers sizes that end exactly on a USB packet (the zero-length
 * packet of the Beta 7.9 crash), many files, rename, move and copy inside a
 * storage and between storages, and deleting single files and whole folders.
 *
 *   cc -O2 -Wall -o mtp-file-ops tools/usb/mtp-file-ops.c -lmtp
 *   ./mtp-file-ops [--many N]
 *
 * Exit 0 only when every step passed. It never touches anything outside its
 * own folder "Y2 USB Test <pid>" and removes that folder at the end. */
#include <libmtp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static LIBMTP_mtpdevice_t *device;
static int failures;
static char scratch[] = "/tmp/y2-mtp-ops-XXXXXX";

static double now(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void step(const char *name, int ok, double started)
{
    /* A step only counts when the player still answers afterwards. */
    int alive = LIBMTP_Get_Storage(device, LIBMTP_STORAGE_SORTBY_NOTSORTED) == 0;
    printf("%-58s %s  %6.2f s%s\n", name, ok && alive ? "PASS" : "FAIL", now() - started,
           alive ? "" : "  (player stopped answering)");
    fflush(stdout);
    if (!ok || !alive) {
        failures++;
        LIBMTP_Dump_Errorstack(device);
    }
    LIBMTP_Clear_Errorstack(device);
    if (!alive) exit(2);
}

static LIBMTP_devicestorage_t *storage(const char *description)
{
    for (LIBMTP_devicestorage_t *s = device->storage; s; s = s->next)
        if (s->StorageDescription && !strcmp(s->StorageDescription, description)) return s;
    return NULL;
}

static void pattern(char *buffer, size_t size, unsigned seed)
{
    for (size_t i = 0; i < size; i++) buffer[i] = (char)((i * 131u + seed * 7u + (i >> 9)) & 0xff);
}

static char *local_file(size_t size, unsigned seed)
{
    static char path[256];
    snprintf(path, sizeof path, "%s/file-%u", scratch, seed);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    char *buffer = malloc(size ? size : 1);
    pattern(buffer, size, seed);
    if (size && fwrite(buffer, 1, size, f) != size) { perror(path); exit(1); }
    fclose(f); free(buffer);
    return path;
}

static int same_as(const char *path, size_t size, unsigned seed)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char *want = malloc(size + 1), *got = malloc(size + 1);
    pattern(want, size, seed);
    size_t n = fread(got, 1, size + 1, f);
    fclose(f);
    int ok = n == size && !memcmp(want, got, size);
    free(want); free(got);
    return ok;
}

static uint32_t send(const char *name, size_t size, unsigned seed, uint32_t folder, uint32_t store)
{
    LIBMTP_file_t *file = LIBMTP_new_file_t();
    file->filename = strdup(name);
    file->filesize = size;
    file->filetype = LIBMTP_FILETYPE_UNKNOWN;
    file->parent_id = folder;
    file->storage_id = store;
    int result = LIBMTP_Send_File_From_File(device, local_file(size, seed), file, NULL, NULL);
    uint32_t id = result ? 0 : file->item_id;
    LIBMTP_destroy_file_t(file);
    return id;
}

static int fetch_matches(uint32_t id, size_t size, unsigned seed)
{
    char path[256];
    snprintf(path, sizeof path, "%s/back-%u", scratch, seed);
    unlink(path);
    return !LIBMTP_Get_File_To_File(device, id, path, NULL, NULL) && same_as(path, size, seed);
}

/* id of `name` directly in `folder` (0 = none); counts the folder's objects. */
static uint32_t find(uint32_t store, uint32_t folder, const char *name, int *count)
{
    uint32_t id = 0; int n = 0;
    LIBMTP_file_t *list = LIBMTP_Get_Files_And_Folders(device, store, folder);
    while (list) {
        LIBMTP_file_t *next = list->next;
        n++;
        if (name && list->filename && !strcmp(list->filename, name)) id = list->item_id;
        LIBMTP_destroy_file_t(list);
        list = next;
    }
    if (count) *count = n;
    return id;
}

int main(int argc, char **argv)
{
    int many = 600;
    if (argc == 3 && !strcmp(argv[1], "--many")) many = atoi(argv[2]);
    if (!mkdtemp(scratch)) { perror("mkdtemp"); return 1; }

    LIBMTP_Init();
    LIBMTP_raw_device_t *raw; int devices;
    if (LIBMTP_Detect_Raw_Devices(&raw, &devices) || devices < 1) { fprintf(stderr, "no MTP device\n"); return 1; }
    device = LIBMTP_Open_Raw_Device_Uncached(&raw[0]);
    if (!device || LIBMTP_Get_Storage(device, LIBMTP_STORAGE_SORTBY_NOTSORTED)) { fprintf(stderr, "cannot open the player\n"); return 1; }
    LIBMTP_devicestorage_t *internal = storage("Internal Storage"), *card = storage("SD Card");
    if (!internal) { fprintf(stderr, "no Internal Storage\n"); return 1; }
    uint32_t in = internal->id, sd = card ? card->id : 0;
    printf("Internal Storage 0x%08x%s\n", in, card ? ", SD Card present" : ", no SD card (between-storage steps skipped)");

    char name[128], top_name[64];
    snprintf(top_name, sizeof top_name, "Y2 USB Test %d", (int)getpid());
    double t = now();
    uint32_t top = LIBMTP_Create_Folder(device, top_name, 0, in);
    step("create the test folder", top != 0, t);

    /* Sizes on and around packet boundaries; s + 12 (container header) = 512·k ends with a ZLP. */
    static const size_t sizes[] = {0, 1, 500, 511, 512, 513, 1012, 4084, 65524, 65536, 65537,
                                   1048564, 1048576, 8u << 20, 40u << 20};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        snprintf(name, sizeof name, "size-%zu.bin", sizes[i]);
        t = now();
        uint32_t id = send(name, sizes[i], 100 + i, top, in);
        int ok = id && fetch_matches(id, sizes[i], 100 + i);
        char label[96]; snprintf(label, sizeof label, "copy to and from the player, %zu bytes", sizes[i]);
        step(label, ok, t);
    }

    t = now();
    uint32_t folder = LIBMTP_Create_Folder(device, "many files", top, in);
    int sent = 0;
    for (int i = 0; folder && i < many; i++) {
        snprintf(name, sizeof name, "song %04d.flac", i);
        sent += send(name, 100 + i % 50, 1000 + i, folder, in) != 0;
    }
    int listed = 0;
    find(in, folder, NULL, &listed);
    char label[96]; snprintf(label, sizeof label, "copy %d small files and list them", many);
    step(label, folder && sent == many && listed == many, t);

    t = now();
    uint32_t song = find(in, top, "size-513.bin", NULL);
    LIBMTP_file_t *meta = song ? LIBMTP_Get_Filemetadata(device, song) : NULL;
    int renamed = meta && !LIBMTP_Set_File_Name(device, meta, "renamed.bin");
    if (meta) LIBMTP_destroy_file_t(meta);
    step("rename a file", renamed && find(in, top, "renamed.bin", NULL) == song && !find(in, top, "size-513.bin", NULL), t);

    t = now();
    int moved = song && !LIBMTP_Move_Object(device, song, in, folder);
    step("move a file into a folder (same storage)",
         moved && find(in, folder, "renamed.bin", NULL) == song && !find(in, top, "renamed.bin", NULL), t);

    t = now();
    uint32_t big = find(in, top, "size-1048576.bin", NULL);
    int copied = big && !LIBMTP_Copy_Object(device, big, in, folder);
    uint32_t copy = find(in, folder, "size-1048576.bin", NULL);
    step("copy a file into a folder (same storage)",
         copied && copy && copy != big && fetch_matches(copy, 1048576, 100 + 12) && find(in, top, "size-1048576.bin", NULL), t);

    if (sd) {
        t = now();
        moved = !LIBMTP_Move_Object(device, folder, sd, 0);
        int count = 0;
        find(sd, folder, NULL, &count);
        step("move a folder with every file to the SD card", moved && find(sd, 0, "many files", NULL) == folder && count == many + 2, t);
        t = now();
        moved = !LIBMTP_Move_Object(device, folder, in, top);
        find(in, folder, NULL, &count);
        step("move it back to Internal Storage", moved && find(in, top, "many files", NULL) == folder && count == many + 2, t);
        t = now();
        copied = !LIBMTP_Copy_Object(device, big, sd, 0);
        copy = find(sd, 0, "size-1048576.bin", NULL);
        int ok = copied && copy && fetch_matches(copy, 1048576, 100 + 12) && !LIBMTP_Delete_Object(device, copy);
        step("copy a file to the SD card and delete it there", ok, t);
    }

    t = now();
    int deleted = 0;
    for (int i = 0; i < many / 2; i++) {
        snprintf(name, sizeof name, "song %04d.flac", i);
        uint32_t id = find(in, folder, name, NULL);
        deleted += id && !LIBMTP_Delete_Object(device, id);
    }
    snprintf(label, sizeof label, "delete %d files one by one", many / 2);
    step(label, deleted == many / 2, t);

    t = now();
    int whole = !LIBMTP_Delete_Object(device, folder);
    step("delete a folder with its remaining files at once", whole && !find(in, top, "many files", NULL), t);

    t = now();
    step("delete the test folder", !LIBMTP_Delete_Object(device, top) && !find(in, 0, top_name, NULL), t);

    LIBMTP_Release_Device(device);
    char command[300]; snprintf(command, sizeof command, "rm -rf '%s'", scratch);
    if (system(command)) {}
    printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
