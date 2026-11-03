/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef Y2_MTP_SAFE_H
#define Y2_MTP_SAFE_H
#include <dirent.h>
#include <stdint.h>
#include <sys/stat.h>
struct y2_upload;
/* Roots are pinned while their storage is mounted into the MTP view and are
 * never reopened behind a removed card: unpinning drops the only reference. */
int y2_fs_init(const char *internal, const char *sd, const char *events);
/* Pin a storage root (must match /run/y2/mtp-expect.txt when listed there). */
int y2_fs_pin(const char *path);
void y2_fs_unpin(const char *path);
int y2_open(const char *path, int flags, mode_t mode);
int y2_stat(const char *path, struct stat64 *st);
DIR *y2_opendir(const char *path);
int y2_remove(const char *path);
int y2_mkdir(const char *path, mode_t mode);
int y2_rename(const char *from, const char *to);
struct y2_upload *y2_upload_begin(const char *path, uint64_t size);
int y2_upload_write(struct y2_upload *upload, const void *data, size_t length);
int y2_upload_finish(struct y2_upload *upload);
void y2_upload_abort(struct y2_upload *upload);
/* A regular file copied like an upload (reserve, temporary name, fsync, no replace). */
int y2_copy_file(const char *from, const char *to);
void y2_transfer_activity(int active, int error);
#endif
