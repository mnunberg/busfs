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
    BUSFS_CONVERT_PATH_EX(path, fqpath);

    res = lstat(fqpath, stbuf);
    if (res == -1) {
        return -errno;
    }

    busfs_file f = busfs_file_get(path, BUSFS_GETf_INC);
    if (f) {
        stbuf->st_mtime = f->mtime;
        stbuf->st_blksize = f->dgram_maxlen;
        stbuf->st_blocks = f->dgram_count;
        stbuf->st_size = f->dgram_count * f->dgram_maxlen;
        busfs_file_release(f, BUSFS_INFO_NONE);
    } else {
        return -ENOENT;
    }

    return 0;
}

int busfs_op_open(const char *path, struct fuse_file_info *fi)
{
    int res;
    int my_errno;
    BUSFS_CONVERT_PATH_EX(path, fqpath);

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


    LOG_MSG("access(%s,%d)", fqpath, acc_flags);
    res = access(fqpath, acc_flags);
    my_errno = errno;
    LOG_MSG("access() returned %d", res);

    if (res != 0) {
        LOG_MSG("Access returned nonzero");
        return -my_errno;
    }

    /* Figure out which kind of object we should provide */

    LOG_MSG("About to request file object");
    f = busfs_file_get(path, BUSFS_GETf_INC);
    LOG_MSG("Have f=%p", f);

    if (!f) {
        return -ENOENT;
    }

    fi->keep_cache = 0;
    fi->direct_io = 1;

    if (acc_flags == R_OK) {
        busfs_reader r = busfs_read_new(f, fi);
        LOG_MSG("Setting reader=%p", r);
        BUSFS_SET_RDR(r, fi);
    } else {
        busfs_writer w = busfs_write_new(f, fi);
        LOG_MSG("Setting writer=%p", w);
        BUSFS_SET_WR(w, fi);
    }

    return 0;
}

/* Same behavior as creating the file, and opening for write-only (manpage) */
int busfs_op_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    BUSFS_CONVERT_PATH_EX(path, fqpath);
    LOG_MSG("Create requested for %s", path);

    int res = creat(fqpath, mode);
    busfs_file f;

    if (res == -1) {
        return -errno;
    }

    f = busfs_file_get(path, BUSFS_GETf_CREATE|BUSFS_GETf_INC);
    if (f == NULL) {
        return -ENOMEM;
    } else {
        busfs_writer w = busfs_write_new(f, fi);
        BUSFS_SET_WR(w, fi);
        return 0;
    }
}

int busfs_op_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    busfs_common o = BUSFS_GET_COMMON(fi);
    if (!o) {
        return -ENOMEM;
    }

    return o->read_func(o, path, buf, size, offset);
}

int busfs_op_write(const char *path, const char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    busfs_common o = BUSFS_GET_COMMON(fi);
    if (!o) {
        return -ENOMEM;
    }

    return o->write_func(o, path, buf, size, offset);
}

int busfs_op_release(const char *path, struct fuse_file_info *fi)
{
    busfs_common o = BUSFS_GET_COMMON(fi);
    if (!o) {
        return -ENOMEM;
    }
    return o->close_func(o, path);
}


int busfs_op_rename(const char *from, const char *to)
{
    BUSFS_CONVERT_PATH_EX(from, fq_from);
    BUSFS_CONVERT_PATH_EX(to, fq_to);

    int ret = 0;
    ret = rename(fq_from, fq_to);

    if (ret != 0) {
        return -errno;
    }

    busfs_file f = busfs_file_get(from, BUSFS_GETf_INC);
    if (!f) {
        return -ENOENT;
    }

    ret = busfs_file_rename(f, to);
    busfs_file_release(f, BUSFS_INFO_NONE);

    return ret;
}

int busfs_op_link(const char *from, const char *to)
{
    (void)from;
    (void)to;
    return -EINVAL;
}

int busfs_op_unlink(const char *path)
{
    int res;
    BUSFS_CONVERT_PATH_EX(path, fqpath);

    res = unlink(fqpath);
    if (res != 0) {
        return -errno;
    }

    busfs_file f = busfs_file_get(path, BUSFS_GETf_INC);

    if (!f) {
        return -ENOENT;
    }

    BUSFS_CONVERT_PATH(path);
    res = busfs_file_unlink(f, path);
    busfs_file_release(f, BUSFS_INFO_NONE);
    return res;
}

