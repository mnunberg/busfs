/**
 * This file contains routines for handles opened for writing
 */

#include "busfs.h"

static int busfs_write_io(busfs_common o,
                   const char *path,
                   const char *buf, size_t size, off_t offset);

static int busfs_write_readfunc(busfs_common o, const char *path,
                                char *buf, size_t size, off_t offset)
{
    (void)path;
    (void)buf;
    (void)size;
    (void)offset;
    (void)o;
    return -EBADF;
}

static int busfs_write_close(busfs_common o, const char *path)
{
    (void)path;
    busfs_file_release((busfs_file)o, BUSFS_INFO_WRITER);
    return 0;
}

busfs_writer busfs_write_new(busfs_file f, struct fuse_file_info *fi)
{
    (void)fi;

    busfs_writer w = (busfs_writer)f;
    pthread_rwlock_wrlock(&f->sync.refs_rwlock);
    f->writer_count++;
    pthread_rwlock_unlock(&f->sync.refs_rwlock);

    w->common.close_func = busfs_write_close;
    w->common.read_func = busfs_write_readfunc;
    w->common.write_func = busfs_write_io;
    w->common.type = BUSFS_INFO_WRITER;

    return w;
}

static void msgs_add_delimited(busfs_file f, const char *buf, size_t size)
{
    char c;
    busfs_dgram *msg = f->dgrams + (size_t)f->curidx;

    while(size) {
        c = *(buf);

        buf++;
        size--;

        if (msg->msgsize >= f->dgram_maxlen) {
            msg->msgsize = (f->dgram_maxlen-1);
            c = f->delim;
        }

        msg->root[msg->msgsize++] = c;

        if (c == f->delim) {
            f->serial++;
            f->curidx++;
            f->curidx %= f->dgram_count;
            msg = f->dgrams + f->curidx;
            msg->serial = f->serial;
            msg->msgsize = 0;
        }
    }
}

static int busfs_write_io(busfs_common o,
                   const char *path,
                   const char *buf, size_t size, off_t offset)
{
    (void)path;
    (void)offset;

    int res;
    busfs_file f = (busfs_file)o;

    if ( (res = pthread_rwlock_wrlock(&f->sync.buf_rwlock)) != 0) {
        return -res;
    }

    msgs_add_delimited(f, buf, size);
    pthread_cond_broadcast(&f->sync.iowait_cond);

    pthread_rwlock_unlock(&f->sync.buf_rwlock);

    f->mtime = time(NULL);

    return size;
}

