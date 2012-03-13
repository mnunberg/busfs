/**
 * This file contains position and data polling for handles opened for reading
 */

#include "busfs.h"
#include "busfs_util.h"

#define _BFG BusFS_Global
#define MINIMUM(a, b) ( ((a) < (b)) ? (a) : (b) )

static void dgram_get_oldest(busfs_file, busfs_dgram **dgramp, uint16_t *idx);

static int busfs_read_io(busfs_common o, const char *path,
                         char *buf, size_t size, off_t offset);

static int busfs_read_close(busfs_common o, const char *path)
{
    busfs_reader r = (busfs_reader)o;
    busfs_file_release(r->f, BUSFS_INFO_READER);
    free(r);
    return 0;
}

static int busfs_read_writefunc(busfs_common o,
                                const char *path,
                                const char *buf, size_t size, off_t offset)
{
    (void)o;
    (void)path;
    (void)size;
    (void)offset;
    return -EBADF;
}

busfs_reader busfs_read_new(busfs_file f, struct fuse_file_info *fi)
{
    busfs_reader ret = calloc(1, sizeof(struct busfs_reader_st));
    busfs_dgram *dgram;

    ret->common.read_func = busfs_read_io;
    ret->common.write_func = busfs_read_writefunc;
    ret->common.close_func = busfs_read_close;
    ret->common.type = BUSFS_INFO_READER;

    ret->f = f;
    ret->r_offset = 0;
    ret->open_flags = fi->flags;

    /* Lock the refcount */
    pthread_rwlock_wrlock(&f->sync.refs_rwlock);

    f->reader_count++;

    pthread_rwlock_unlock(&f->sync.refs_rwlock);

    pthread_rwlock_rdlock(&f->sync.buf_rwlock);
    dgram_get_oldest(f, &dgram, &ret->r_idx);
    ret->r_serial = dgram->serial;
    pthread_rwlock_unlock(&f->sync.buf_rwlock);

    return ret;
}

/**
 * This function gets the next message for the reader r, updating its
 * current position variables.
 */
static busfs_dgram *get_next_message(busfs_reader r, busfs_dgram *msg)
{
    uint16_t nextidx = (r->r_idx+1) % (r->f->dgram_count);
    busfs_dgram *nextmsg = r->f->dgrams + nextidx;
    uint32_t diff = nextmsg->serial - msg->serial;

    if (diff != 1) {
        /* Overflow */
        return NULL;
    }

    /* Fill in the next datagram */
    msg = nextmsg;
    r->r_idx = nextidx;
    r->r_serial = nextmsg->serial;
    r->r_offset = 0;
    return msg;
}

/**
 * This helper function tries to read size data from the ringbuffer,
 * returning the amount of bytes left to read.
 */
static ssize_t read_file(busfs_reader r, char *dst, size_t size)
{
    busfs_dgram *msg = r->f->dgrams + r->r_idx;
    size_t origsize = size, total = 0;

    while (size) {
        char *src = msg->root;
        size_t toCopy = msg->msgsize;

        toCopy -= r->r_offset;
        toCopy = MINIMUM(size, toCopy);

        src += r->r_offset;

        if (toCopy == 0) {
            msg = get_next_message(r, msg);
            if (msg == NULL) {
                break;
            } else {
                continue;
            }
        }

        memcpy(dst, src, toCopy);
        size -= toCopy;
        dst += toCopy;
        total += toCopy;

        r->r_offset += toCopy;
    }

    if (size == origsize) {
        return -EAGAIN;
    }
    LOG_MSG("READ: Returning %lu", total);
    return total;
}

/**
 * Get the oldest datagram in the ringbuffer.
 */
static void dgram_get_oldest(busfs_file f, busfs_dgram **dgramp, uint16_t *idx)
{
    uint16_t diff = f->curidx - (f->dgram_count-1);
    *idx = f->curidx - diff;
    *dgramp = f->dgrams + (size_t)(*idx);

    if ( (*dgramp)->msgsize == 0) {
        uint16_t idx_start = *idx;
        do {

            *dgramp = f->dgrams + (size_t)(*idx);
            if ( (*dgramp)->msgsize) {
                break;
            }

            *(idx) += 1;
            *idx %= (f->dgram_count);

        } while(*idx != idx_start);

        if (*idx == idx_start) {
            *idx = f->curidx;
            *dgramp = f->dgrams;
        }
    }

}

/**
 * Signal handler, to increase the current thread's iowait sequence.
 */
void busfs_read_interrupt_handler(int sig)
{
    /* Set the cancellation key */
    unsigned int *curseqp =
            (unsigned int*)pthread_getspecific(_BFG.iowait_seq_key);

    if (curseqp == NULL) {
        LOG_MSG("No current seq_key");
        return;
    }

    *curseqp += 1;
    LOG_MSG("Increasing iowait sequence: %d => %d", *curseqp-1, *curseqp);
}

static inline void mk_condwait_tmo(struct timespec *timeout)
{
    struct timeval now, offset, result;
    gettimeofday(&now, NULL);
    offset.tv_sec = 0;
    offset.tv_usec = 250000;
    busfs_timeval_add(&result, &now, &offset);
    timeout->tv_sec = result.tv_sec;
    timeout->tv_nsec = result.tv_usec * 1000;
}

/**
 * Wait for more data arrives in the ringbuffer, or the operation is interrupted
 */
static inline int wait_for_more_data(busfs_reader r,
                                     busfs_dgram *msg,
                                     uint32_t current_serial, size_t current_size)
{
    int status;
    int ret = 0;

    unsigned int waitseq = 42;
    unsigned int start_seq = waitseq;
    int do_loop = 1;

    pthread_setspecific(_BFG.iowait_seq_key, &waitseq);

    LOG_MSG("Will try and wait for updates... (wait=%u,start=%u)",
            waitseq,start_seq);
    status = -1;

#define _HAVE_NEW_DATA \
    (r->f->serial != current_serial || msg->msgsize != current_size \
            || (r->f->unlinked && r->f->writer_count == 0) )

    while(do_loop) {
        struct timespec timeout;

        if (waitseq != start_seq) {
            LOG_MSG("Detected interrupt");
            ret = -EINTR;
            /* Not locked yet */
            goto GT_RET;
        }

        pthread_mutex_lock(&r->f->sync.iowait_mutex);

        /* We already have data, no need to wait for cond signal */

        if (_HAVE_NEW_DATA) {
            ret = 0;
            goto GT_UNLOCK;
        }

        mk_condwait_tmo(&timeout);
        status = pthread_cond_timedwait(&r->f->sync.iowait_cond,
                                        &r->f->sync.iowait_mutex,
                                        &timeout);

        if (_HAVE_NEW_DATA) {
            ret = 0;
            goto GT_UNLOCK;
        }
        pthread_mutex_unlock(&r->f->sync.iowait_mutex);
    }

#undef _HAVE_NEW_DATA

    GT_UNLOCK:
    pthread_mutex_unlock(&r->f->sync.iowait_mutex);

    GT_RET:
    pthread_setspecific(_BFG.iowait_seq_key, NULL);
    LOG_MSG("Returning %d", ret);
    return ret;
}

static int busfs_read_io(busfs_common o, const char *path,
                         char *buf, size_t size, off_t offset)
{
    int ret, my_errno = 0;
    int status;
    busfs_reader r = (busfs_reader)o;
    (void)offset;


    GT_BEGIN:
    pthread_rwlock_rdlock(&r->f->sync.buf_rwlock);

    busfs_dgram *msg = r->f->dgrams + r->r_idx;
    LOG_MSG("Current index is %d", r->r_idx);
    LOG_MSG("Current serial is %lu", r->r_serial);

    if (msg->serial == r->r_serial && msg->msgsize == r->r_offset) {
        /* No change since last read */

        uint32_t current_serial = r->f->serial;
        size_t current_size = msg->msgsize;
        pthread_rwlock_unlock(&r->f->sync.buf_rwlock);

        if (r->open_flags & O_NONBLOCK) {
            return -EWOULDBLOCK;
        }

        status = wait_for_more_data(r, msg, current_serial, current_size);

        if (status == 0) {
            LOG_MSG("Size(%lu,%lu), Serial(%lu,%lu)",
                    msg->msgsize, current_size,
                    r->f->serial, msg->serial);
            /* Check which condition has changed:
             * if the serial has changed, increase the reader's serial (and index).
             * Otherwise, assume more data has trickled into the current
             * offset.
             */
            goto GT_BEGIN;
        } else {
            return status;
        }
    }

    my_errno = 0;
    /* So we have more data */
    if (msg->serial == r->r_serial) {
        goto GT_RET;

    } else {

        /* We've had a ringbuffer wrap-around.
         * This obviously means we've skipped some messages,
         *
         */
        dgram_get_oldest(r->f, &msg, &r->r_idx);
        LOG_MSG("Rollover index: %d", r->r_idx);
        r->r_serial = msg->serial;
        r->r_offset = 0;
        goto GT_RET;
    }


    GT_RET:
    ret = read_file(r, buf, size);
    pthread_rwlock_unlock(&r->f->sync.buf_rwlock);
    return ret;
}
