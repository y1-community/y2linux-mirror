/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE 1
#endif
#define _FILE_OFFSET_BITS 64
#include "mtp-safe.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define RESERVE_BYTES (96ULL * 1024 * 1024)
#define RESERVE_INODES 128
#define MAX_ROOTS 4
static struct { char *path; int fd; } roots[MAX_ROOTS] = {{NULL, -1}, {NULL, -1}, {NULL, -1}, {NULL, -1}};
#define EXPECT_PATH "/run/y2/mtp-expect.txt"
#define ROOTS_PATH "/run/y2/mtp-roots.json"
#define SPACE_CHECK_BYTES (8ULL * 1024 * 1024)
static const char *event_path;
static uint64_t serial, generation;
struct y2_upload { int fd, parent; char *name; char temporary[80]; uint64_t size, written, checked; };

/* Contains no filenames or credentials. Atomic, bounded activity/generation
 * receipt consumed by the platform; generations advance only after durability. */
static void event(int active, int error, int changed)
{
    if (!event_path) return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (changed) generation = (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
    char temporary[256], data[256];
    if (snprintf(temporary, sizeof temporary, "%s.%ld", event_path, (long)getpid()) >= (int)sizeof temporary) return;
    int length = snprintf(data, sizeof data,
        "{\"schema\":1,\"pid\":%ld,\"active\":%s,\"error\":%d,\"generation\":%llu}\n",
        (long)getpid(), active ? "true" : "false", error, (unsigned long long)generation);
    int fd = open(temporary, O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC, 0600);
    if (fd < 0) return;
    if (write(fd, data, length) == length) rename(temporary, event_path);
    close(fd);
    unlink(temporary);
}
void y2_transfer_activity(int active, int error) { event(active, error, 0); }

/* Atomic receipt of the currently pinned roots: the platform waits for a root
 * to disappear from it before it unmounts a card, and for it to appear before
 * it announces the card. Contains paths of the two fixed views only. */
static void publish_roots(void)
{
    char temporary[96], data[256];
    int length = snprintf(data, sizeof data, "{\"schema\":1,\"pid\":%ld,\"roots\":[", (long)getpid());
    int first = 1;
    for (unsigned i = 0; i < MAX_ROOTS && length < (int)sizeof data - 48; i++) {
        if (!roots[i].path || roots[i].fd < 0) continue;
        length += snprintf(data + length, sizeof data - length, "%s\"%s\"", first ? "" : ",", roots[i].path);
        first = 0;
    }
    length += snprintf(data + length, sizeof data - length, "]}\n");
    snprintf(temporary, sizeof temporary, ROOTS_PATH ".%ld", (long)getpid());
    int fd = open(temporary, O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC, 0600);
    if (fd < 0) return;
    if (write(fd, data, length) == length) rename(temporary, ROOTS_PATH);
    close(fd);
    unlink(temporary);
}

/* The platform writes "<path> <major>:<minor> <mount_id>" per view it validated.
 * A pin of a different mount (card swapped between validation and pinning) fails. */
static int expected(const char *path, int fd)
{
    FILE *file = fopen(EXPECT_PATH, "re");
    if (!file) return 0;
    char line[160], name[96];
    unsigned major_number, minor_number;
    unsigned long long mount_id;
    int listed = 0, ok = 1;
    while (fgets(line, sizeof line, file)) {
        if (sscanf(line, "%95s %u:%u %llu", name, &major_number, &minor_number, &mount_id) != 4 || strcmp(name, path)) continue;
        listed = 1;
        struct stat64 info;
        ok = !fstat64(fd, &info) && major(info.st_dev) == major_number && minor(info.st_dev) == minor_number;
        /* The mount id of the pinned directory comes from the descriptor itself. */
        char fdinfo[64], text[256];
        snprintf(fdinfo, sizeof fdinfo, "/proc/self/fdinfo/%d", fd);
        FILE *info_file = ok ? fopen(fdinfo, "re") : NULL;
        int found = 0;
        while (info_file && fgets(text, sizeof text, info_file)) {
            unsigned long long id;
            if (sscanf(text, "mnt_id: %llu", &id) == 1) { found = 1; ok = id == mount_id; }
        }
        if (info_file) fclose(info_file);
        if (!found) ok = 0;
    }
    fclose(file);
    return listed && !ok ? -1 : 0;
}

int y2_fs_pin(const char *path)
{
    for (unsigned i = 0; i < MAX_ROOTS; i++)
        if (roots[i].path && !strcmp(roots[i].path, path) && roots[i].fd >= 0) return 0;
    for (unsigned i = 0; i < MAX_ROOTS; i++) {
        if (roots[i].path) continue;
        int fd = open(path, O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if (fd < 0) return -1;
        if (expected(path, fd)) { close(fd); errno = ESTALE; return -1; }
        roots[i].path = strdup(path);
        if (!roots[i].path) { close(fd); return -1; }
        roots[i].fd = fd;
        publish_roots();
        return 0;
    }
    errno = ENOSPC;
    return -1;
}

void y2_fs_unpin(const char *path)
{
    for (unsigned i = 0; i < MAX_ROOTS; i++) {
        if (!roots[i].path || strcmp(roots[i].path, path)) continue;
        if (roots[i].fd >= 0) close(roots[i].fd);
        free(roots[i].path);
        roots[i].path = NULL;
        roots[i].fd = -1;
        publish_roots();
    }
}

int y2_fs_init(const char *internal, const char *sd, const char *events)
{
    const char *paths[] = {internal, sd};
    event_path = events;
    for (unsigned i = 0; i < MAX_ROOTS; i++) {
        if (roots[i].fd >= 0) close(roots[i].fd);
        free(roots[i].path);
        roots[i].path = NULL;
        roots[i].fd = -1;
    }
    for (unsigned i = 0; i < 2; i++)
        if (paths[i] && y2_fs_pin(paths[i])) return -1;
    publish_roots();
    event(0, 0, 0);
    return 0;
}

/* No block device, symlink, magic-link or nested mount can be resolved. Fail
 * closed on kernels without openat2. root fd preserves removed-media identity. */
static int resolve(const char *path, const char **relative)
{
    for (unsigned i = 0; i < MAX_ROOTS; i++) {
        if (!roots[i].path || roots[i].fd < 0) continue;
        size_t length = strlen(roots[i].path);
        if (!strncmp(path, roots[i].path, length) && (path[length] == '/' || !path[length])) {
            *relative = path + length;
            while (**relative == '/') (*relative)++;
            if (!**relative) *relative = ".";
            return roots[i].fd;
        }
    }
    errno = EACCES;
    return -1;
}

static int beneath(int root, const char *relative, int flags, mode_t mode)
{
    struct open_how how = {.flags = (uint64_t)(flags|O_CLOEXEC|O_NOFOLLOW),
        .mode = (flags & O_CREAT) ? mode : 0,
        .resolve = RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS|RESOLVE_NO_XDEV};
    return syscall(SYS_openat2, root, relative, &how, sizeof how);
}

static int parent(const char *path, char **name)
{
    const char *relative;
    int root = resolve(path, &relative);
    if (root < 0) return -1;
    char *copy = strdup(relative);
    if (!copy) return -1;
    char *slash = strrchr(copy, '/');
    *name = strdup(slash ? slash+1 : copy);
    if (!*name || !**name || !strcmp(*name, ".") || !strcmp(*name, "..") || !strncmp(*name, ".y2-mtp-", 8)) {
        free(*name); *name = NULL; free(copy); errno = EACCES; return -1;
    }
    if (slash) *slash = 0;
    int fd = beneath(root, slash ? copy : ".", O_RDONLY|O_DIRECTORY, 0);
    free(copy);
    if (fd < 0) { free(*name); *name = NULL; }
    return fd;
}

int y2_open(const char *path, int flags, mode_t mode)
{
    const char *relative;
    int root = resolve(path, &relative);
    if (root < 0 || (flags & (O_CREAT|O_TRUNC|O_WRONLY|O_RDWR))) { errno = EACCES; return -1; }
    int fd = beneath(root, relative, flags|O_NONBLOCK, mode);
    struct stat64 st;
    if (fd >= 0 && (fstat64(fd, &st) || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) ||
                   (S_ISREG(st.st_mode) && st.st_nlink > 1))) {
        close(fd); errno = EACCES; return -1;
    }
    return fd;
}
int y2_stat(const char *path, struct stat64 *st)
{
    int fd = y2_open(path, O_RDONLY, 0);
    if (fd < 0) return -1;
    int result = fstat64(fd, st); close(fd); return result;
}
DIR *y2_opendir(const char *path)
{
    int fd = y2_open(path, O_RDONLY|O_DIRECTORY, 0);
    if (fd < 0) return NULL;
    DIR *dir = fdopendir(fd); if (!dir) close(fd); return dir;
}
int y2_remove(const char *path)
{
    char *name;
    int fd = parent(path, &name);
    if (fd < 0) return -1;
    struct stat64 st;
    int result = fstatat64(fd, name, &st, AT_SYMLINK_NOFOLLOW);
    if (!result) result = unlinkat(fd, name, S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0);
    if (!result) result = fsync(fd);
    if (!result) event(0, 0, 1);
    free(name); close(fd); return result;
}
int y2_mkdir(const char *path, mode_t mode)
{
    char *name;
    int fd = parent(path, &name);
    if (fd < 0) return -1;
    int result = mkdirat(fd, name, mode);
    if (!result) result = fsync(fd);
    if (!result) event(0, 0, 1);
    free(name); close(fd); return result;
}
int y2_rename(const char *from, const char *to)
{
    char *first = NULL, *second = NULL;
    int a = parent(from, &first), b = parent(to, &second), result = -1;
    if (a >= 0 && b >= 0) {
        /* A symlink is never host visible and cannot be renamed into a view. */
        struct stat64 st;
        if (!fstatat64(a, first, &st, AT_SYMLINK_NOFOLLOW) && (S_ISREG(st.st_mode)||S_ISDIR(st.st_mode))) {
            result = syscall(SYS_renameat2, a, first, b, second, RENAME_NOREPLACE);
            if (!result) result = fsync(a) || fsync(b) ? -1 : 0;
            if (!result) event(0, 0, 1);
        }
    }
    if (a >= 0) close(a);
    if (b >= 0) close(b);
    free(first); free(second); return result;
}
static int space(int fd, uint64_t size)
{
    struct statvfs info;
    if (fstatvfs(fd, &info)) return -1;
    uint64_t available = (uint64_t)info.f_bavail * info.f_frsize;
    if (available < RESERVE_BYTES || size > available-RESERVE_BYTES ||
        (info.f_files && info.f_favail <= RESERVE_INODES)) { errno = ENOSPC; return -1; }
    return 0;
}
struct y2_upload *y2_upload_begin(const char *path, uint64_t size)
{
    struct y2_upload *u = calloc(1, sizeof *u);
    if (!u) { event(0, ENOMEM, 0); return NULL; }
    u->fd = -1; u->parent = -1; u->size = size;
    u->parent = parent(path, &u->name);
    if (u->parent < 0 || space(u->parent, size == UINT32_MAX ? 0 : size)) goto fail;
    struct stat64 st;
    if (!fstatat64(u->parent, u->name, &st, AT_SYMLINK_NOFOLLOW) || errno != ENOENT) { errno = EEXIST; goto fail; }
    snprintf(u->temporary, sizeof u->temporary, ".y2-mtp-%ld-%llu.part", (long)getpid(), (unsigned long long)++serial);
    u->fd = openat(u->parent, u->temporary, O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC, 0600);
    if (u->fd < 0) goto fail;
    event(1, 0, 0);
    return u;
fail:
    { int error = errno; y2_upload_abort(u); event(0, error, 0); errno = error; return NULL; }
}
int y2_upload_write(struct y2_upload *u, const void *data, size_t length)
{
    if (!u) { errno = EINVAL; return -1; }
    /* The reserve is enforced at admission and then every 8 MiB; a statvfs per
     * 64 KiB write only costs syscalls. ENOSPC from write() still aborts. */
    if (u->written + length >= u->checked + SPACE_CHECK_BYTES || !u->checked) {
        uint64_t need = SPACE_CHECK_BYTES;
        if (u->size != UINT32_MAX && u->size > u->written && u->size - u->written < need) need = u->size - u->written;
        if (space(u->fd, need)) return -1;
        u->checked = u->written + length;
    }
    if (u->size != UINT32_MAX && length > u->size-u->written) { errno = EMSGSIZE; return -1; }
    const char *bytes = data;
    size_t left = length;
    while (left) {
        ssize_t written = write(u->fd, bytes, left);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { if (!written) errno = EIO; return -1; }
        left -= written; bytes += written; u->written += written;
    }
    return 0;
}
int y2_upload_finish(struct y2_upload *u)
{
    int result = -1;
    if (u && (u->size == UINT32_MAX || u->size == u->written) && !fsync(u->fd)) {
        result = syscall(SYS_renameat2, u->parent, u->temporary, u->parent, u->name, RENAME_NOREPLACE);
        if (!result) result = fsync(u->parent);
    } else if (u && u->size != u->written) errno = EIO;
    int error = result ? errno : 0;
    y2_upload_abort(u);
    event(0, error, !result);
    errno = error;
    return result;
}
void y2_upload_abort(struct y2_upload *u)
{
    if (!u) return;
    if (u->fd >= 0) close(u->fd);
    if (u->parent >= 0) { if (*u->temporary) unlinkat(u->parent, u->temporary, 0); close(u->parent); }
    free(u->name); free(u);
    event(0, 0, 0);
}

/* One regular file copied the way an upload arrives (MTP CopyObject, and
 * MoveObject between storages): the space reserve, a hidden temporary name,
 * fsync and a publish that never replaces an existing name. The source keeps
 * its modification time. Nothing is left behind on failure. */
int y2_copy_file(const char *from, const char *to)
{
    int in = y2_open(from, O_RDONLY, 0);
    if (in < 0) return -1;
    struct stat64 st;
    if (fstat64(in, &st) || !S_ISREG(st.st_mode)) { close(in); errno = EACCES; return -1; }
    /* The upload size field reserves UINT32_MAX for "unknown"; nothing that
     * large is copied on the device (a computer copies it instead). */
    if ((uint64_t)st.st_size >= UINT32_MAX) { close(in); errno = EFBIG; return -1; }
    struct y2_upload *u = y2_upload_begin(to, (uint64_t)st.st_size);
    if (!u) { int error = errno; close(in); errno = error; return -1; }
    static char buffer[64 * 1024];
    uint64_t copied = 0;
    while (copied < (uint64_t)st.st_size) {
        ssize_t n = read(in, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || y2_upload_write(u, buffer, (size_t)n)) {
            int error = n < 0 ? errno : (n == 0 ? EIO : errno);
            y2_upload_abort(u); close(in); errno = error; return -1;
        }
        copied += (uint64_t)n;
    }
    close(in);
    struct timespec times[2] = {st.st_atim, st.st_mtim};
    futimens(u->fd, times);
    return y2_upload_finish(u);
}
