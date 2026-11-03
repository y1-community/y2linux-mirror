/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>
static int write_fault, sync_fault, space_fault;
static ssize_t test_write(int fd, const void *data, size_t size)
{
    if (write_fault == 1) { write_fault=3; errno=EINTR; return -1; }
    if (write_fault == 2) { errno=ENOSPC; return -1; }
    return write(fd, data, write_fault == 3 && size > 1 ? size/2 : size);
}
static int test_sync(int fd) { if (sync_fault) { errno=EIO; return -1; } return fsync(fd); }
static int test_space(int fd, struct statvfs *info)
{
    int result=fstatvfs(fd, info);
    if (!result && space_fault) info->f_bavail=0;
    return result;
}
#define write test_write
#define fsync test_sync
#define fstatvfs test_space
#include "../tools/usb/mtp-safe.c"
#undef write
#undef fsync
#undef fstatvfs
int main(int argc, char **argv)
{
    assert(argc == 2);
    char path[4096], other[4096], outside[4096], eventfile[4096];
    snprintf(eventfile,sizeof eventfile,"%s/events.json",argv[1]);
    assert(!y2_fs_init(argv[1], NULL, eventfile));
    snprintf(path,sizeof path,"%s/song.flac",argv[1]);
    struct y2_upload *u=y2_upload_begin(path,6); assert(u);
    assert(access(path,F_OK));
    write_fault=1; assert(!y2_upload_write(u,"abcdef",6)); write_fault=0;
    assert(!y2_upload_finish(u));
    int fd=y2_open(path,O_RDONLY,0); assert(fd>=0);
    char bytes[7]={0}; assert(read(fd,bytes,6)==6 && !strcmp(bytes,"abcdef")); close(fd);
    assert(!y2_upload_begin(path,6)); /* no overwrite of existing music */
    assert(y2_open(path,O_TRUNC|O_WRONLY,0)<0);
    snprintf(other,sizeof other,"%s/partial.mp3",argv[1]);
    u=y2_upload_begin(other,6); assert(u); assert(!y2_upload_write(u,"ab",2));
    assert(y2_upload_finish(u)<0 && access(other,F_OK));
    u=y2_upload_begin(other,6); assert(u); write_fault=2;
    assert(y2_upload_write(u,"abcdef",6)<0); write_fault=0; y2_upload_abort(u);
    assert(access(other,F_OK));
    u=y2_upload_begin(other,6); assert(u); assert(!y2_upload_write(u,"abcdef",6)); sync_fault=1;
    assert(y2_upload_finish(u)<0); sync_fault=0; assert(access(other,F_OK));
    space_fault=1; assert(!y2_upload_begin(other,6)); space_fault=0;
    fd=open(eventfile,O_RDONLY); assert(fd>=0); char eventdata[256]={0};
    assert(read(fd,eventdata,sizeof eventdata-1)>0); close(fd);
    assert(strstr(eventdata,"\"error\":28") && strstr(eventdata,"\"active\":false"));
    snprintf(path,sizeof path,"%s/one-MiB.flac",argv[1]);
    u=y2_upload_begin(path,1024*1024); assert(u); char block[65536]; memset(block,'a',sizeof block);
    for(unsigned i=0;i<16;i++) assert(!y2_upload_write(u,block,sizeof block));
    assert(!y2_upload_finish(u));

    /* Replacing the path cannot overwrite a racing writer. */
    u=y2_upload_begin(other,6); assert(u); assert(!y2_upload_write(u,"abcdef",6));
    fd=open(other,O_CREAT|O_EXCL|O_WRONLY,0600); assert(fd>=0); close(fd);
    assert(y2_upload_finish(u)<0);
    snprintf(outside,sizeof outside,"%s/../protected",argv[1]);
    assert(!y2_upload_begin(outside,6));
    snprintf(other,sizeof other,"%s/link",argv[1]); assert(!symlink("/etc",other));
    snprintf(outside,sizeof outside,"%s/link/passwd",argv[1]); assert(y2_open(outside,O_RDONLY,0)<0);
    snprintf(outside,sizeof outside,"%s-other/song",argv[1]); assert(y2_open(outside,O_RDONLY,0)<0);
    snprintf(outside,sizeof outside,"%s/fifo",argv[1]); assert(!mkfifo(outside,0600)); assert(y2_open(outside,O_RDONLY,0)<0);
    /* Directory operations obey the same parent pins and fsync contract. */
    snprintf(other,sizeof other,"%s/album",argv[1]); assert(!y2_mkdir(other,0700));
    snprintf(outside,sizeof outside,"%s/renamed",argv[1]); assert(!y2_rename(other,outside));
    assert(!y2_remove(outside));
    /* Unknown (>=4 GiB MTP size sentinel) has 64-bit streamed accounting. */
    snprintf(other,sizeof other,"%s/large.flac",argv[1]);
    u=y2_upload_begin(other,UINT32_MAX); assert(u); assert(!y2_upload_write(u,"abcdef",6)); assert(!y2_upload_finish(u));
    /* 100 completed small files; metadata fsync and content verification. */
    for(unsigned i=0;i<100;i++) {
        snprintf(other,sizeof other,"%s/%u.mp3",argv[1],i);
        u=y2_upload_begin(other,6); assert(u); assert(!y2_upload_write(u,"abcdef",6)); assert(!y2_upload_finish(u));
    }
    puts("PASS atomic writes, short/EINTR/ENOSPC/fsync failure, cancellation, race, traversal, symlink, special file, 64-bit streaming, small files");
}
