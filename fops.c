/**
 * This file contains wrapping functions which need special handling.
 * See boilerplate.c for the simple wrappers
 */

#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "busfs.h"

int busfs_op_getattr(const char *path, struct stat *stbuf)
{
    int res;
    BUSFS_CONVERT_PATH(path);

    res = lstat(path, stbuf);
    if (res == -1) {
        return -errno;
    }

    busfs_file f = busfs_file_get(path, 0, 0);
    if (f) {
        stbuf->st_mtime = f->mtime;
    }

    return 0;
}

int busfs_op_open(const char *path, struct fuse_file_info *fi)
{
    int res;
    BUSFS_CONVERT_PATH(path);
    busfs_file f;

    int acc_flags = 0;
    int accmode = (fi->flags & O_ACCMODE);

    switch(accmode) {
    case O_RDONLY:
        acc_flags = R_OK;
        break;
    case O_WRONLY:
        acc_flags = W_OK;
        break;
    default:
        LOG_MSG("Unsupported mode %d", accmode);
        return -EINVAL;
    }

    LOG_MSG("access(%s,%d)", path, acc_flags);
    res = access(path, acc_flags);
    LOG_MSG("access() returned %d", res);

    if (res != 0) {
        LOG_MSG("Access returned nonzero");
        return -errno;
    }

    LOG_MSG("About to request file object");
    f = busfs_file_get(path, 1, 1);
    LOG_MSG("Have f=%p", f);

    if (!f) {
        return -ENOMEM;
    }

    fi->nonseekable = 1;
    fi->keep_cache = 0;
    fi->direct_io = 1;

    if (acc_flags == R_OK) {
        busfs_reader r = busfs_reader_new(f);
        LOG_MSG("Setting reader=%p", r);
        BUSFS_SET_RDR(r, fi);
    } else {
        LOG_MSG("Setting writer=%p", f);
        BUSFS_SET_FI(f, fi);
    }

    return 0;
}

int busfs_op_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    BUSFS_CONVERT_PATH(path);

    LOG_MSG("Requested to read %lu bytes into %p at offset=%ld", size, buf, offset);
    busfs_reader r = BUSFS_GET_RDR(fi);
    LOG_MSG("We have reader %p", r);
    if (!r) {
        LOG_MSG("Requested read without handle?");
        return -EFAULT;
    }

    (void)offset;
    return busfs_read_file(r, fi, buf, size);
}

int busfs_op_write(const char *path, const char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    BUSFS_CONVERT_PATH(path);
    LOG_MSG("Path requested: %s", path);
    busfs_file f = BUSFS_GET_FI(fi);
    if (f == NULL) {
        LOG_MSG("Requested to write() to a file we have no handle on?");
        return -EINVAL;
    }

    return busfs_file_write(f, buf, size);
}

int busfs_op_release(const char *path, struct fuse_file_info *fi)
{
    /* Just a stub.  This method is optional and can safely be left
       unimplemented */
    BUSFS_CONVERT_PATH(path);
    int openflags = (fi->flags & O_ACCMODE);
    if (openflags == O_RDONLY) {
        busfs_reader r = BUSFS_GET_RDR(fi);
        busfs_reader_free(r);
    } else {
        /* We should release the handle here ?*/
    }

    (void) path;
    return 0;
}


int busfs_op_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    BUSFS_CONVERT_PATH(path);
    LOG_MSG("Create requested for %s", path);
    int res = creat(path, mode);
    busfs_file f;

    if (res == -1) {
        return -errno;
    }

    f = busfs_file_get(path, 1, 1);
    if (f == NULL) {
        return -ENOMEM;
    }

    BUSFS_SET_FI(f, fi);

    return res;
}

