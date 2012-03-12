#include "busfs.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>

#include "busfs_util.h"

struct busfs_global_st BusFS_Global;
FILE *busfs_log_output;

#define _BFG BusFS_Global
#define MINIMUM(a, b) ( ((a) < (b)) ? (a) : (b) )

static void dgram_get_oldest(busfs_file, busfs_dgram **dgramp, uint16_t *idx);

static void handle_interrupts(int sig)
{
    LOG_MSG("I was interrupted!");
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

void busfs_init(void)
{
    struct sigaction sa;

    assert(pthread_rwlock_init(&_BFG.lock, NULL) == 0);
    LOG_MSG("Lock initialized for hashtable");

    BusFS_Global.ht = g_hash_table_new(g_str_hash, g_str_equal);
    assert(_BFG.ht);
    LOG_MSG("Hash table initialized");

    assert( pthread_key_create(&_BFG.iowait_seq_key, NULL) == 0);

    /* Initialize signal handlers, maybe?*/
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_interrupts;
    sigaction(SIGUSR1, &sa, NULL);

}

/**
 * Just initialize some variables:
 */
static busfs_file new_busfs_file(const char *path)
{
    busfs_file f;

    f = calloc(1, sizeof(struct busfs_file_st));

    strncpy(f->path, path, sizeof(f->path));

    f->delim = '\n';
    f->serial = 0x100;
    f->dgrams[0].serial = f->serial;

    pthread_rwlock_init(&f->sync.refs_rwlock, NULL);
    pthread_rwlock_init(&f->sync.buf_rwlock, NULL);
    pthread_cond_init(&f->sync.iowait_cond, NULL);
    pthread_mutex_init(&f->sync.iowait_mutex, NULL);
    return f;
}

busfs_file busfs_file_get(const char *path, int create, int inc)
{
    busfs_file f;
    int ret;
    LOG_MSG("Requested file object for %s", path);

    ret = pthread_rwlock_rdlock(&_BFG.lock);

    if (ret != 0) {
        LOG_MSG("Couldn't lock: %s", strerror(errno));
        return NULL;
    }

    LOG_MSG("Locked hashtable via rdlock");


    f = (busfs_file)g_hash_table_lookup(_BFG.ht, path);

    LOG_MSG("Initial lookup returns %p", f);

    if (f && inc) {
        LOG_MSG("Incrementing the refcount");
        pthread_rwlock_wrlock(&f->sync.refs_rwlock);
        f->refcount++;
        pthread_rwlock_unlock(&f->sync.refs_rwlock);
    }

    pthread_rwlock_unlock(&_BFG.lock);

    if (f) {
        return f;
    } else {
        if (!create) {
            return NULL;
        }

        ret = pthread_rwlock_wrlock(&_BFG.lock);
        if (ret != 0) {
            return NULL;
        }

        f = new_busfs_file(path);
        g_hash_table_insert(_BFG.ht, f->path, f);

        f->refcount = (inc) ? 1 : 0;

        pthread_rwlock_unlock(&_BFG.lock);
        return f;
    }
}

static void msgs_add_delimited(busfs_file f, const char *buf, size_t size)
{
    char c;
    busfs_dgram *msg = f->dgrams + (size_t)f->curidx;

    while(size) {
        c = *(buf);

        buf++;
        size--;

        if (msg->msgsize >= BUSFS_MSGLEN_INITIAL) {
            msg->msgsize = BUSFS_MSGLEN_INITIAL-1;
            c = f->delim;
        }

        msg->root[msg->msgsize++] = c;

        if (c == f->delim) {
            f->serial++;
            f->curidx++;
            f->curidx %= BUSFS_DGRAM_COUNT;
            msg = f->dgrams + f->curidx;
            msg->serial = f->serial;
            msg->msgsize = 0;
        }
    }
}

int busfs_file_write(busfs_file f, const char *buf, size_t size)
{

    if (pthread_rwlock_wrlock(&f->sync.buf_rwlock) != 0) {
        return -errno;
    }

    msgs_add_delimited(f, buf, size);
    pthread_cond_broadcast(&f->sync.iowait_cond);

    pthread_rwlock_unlock(&f->sync.buf_rwlock);

    return size;

}

busfs_reader busfs_reader_new(busfs_file f)
{
    busfs_reader ret = calloc(1, sizeof(struct busfs_reader_st));
    busfs_dgram *dgram;
    ret->f = f;
    ret->r_offset = 0;

    /* Lock the refcount */
    pthread_rwlock_wrlock(&f->sync.refs_rwlock);
    f->refcount++;
    pthread_rwlock_unlock(&f->sync.refs_rwlock);


    pthread_rwlock_rdlock(&f->sync.buf_rwlock);
    dgram_get_oldest(f, &dgram, &ret->r_idx);
    ret->r_serial = dgram->serial;
    pthread_rwlock_unlock(&f->sync.buf_rwlock);

    return ret;
}

void busfs_reader_free(busfs_reader r)
{
    LOG_MSG("Freeing r=%p", r);
    free(r);
}

/**
 * This function gets the next message for the reader r, updating its
 * current position variables.
 */
static busfs_dgram *get_next_message(busfs_reader r, busfs_dgram *msg)
{
    uint16_t nextidx = (r->r_idx+1) % (BUSFS_DGRAM_COUNT);
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
    uint16_t diff = f->curidx - (BUSFS_DGRAM_COUNT-1);
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
            *idx %= (BUSFS_DGRAM_COUNT);

        } while(*idx != idx_start);

        if (*idx == idx_start) {
            *idx = f->curidx;
            *dgramp = f->dgrams;
        }
    }

}
/**
 * Wait for more data arrives in the ringbuffer, or the operation is interrupted
 */
static inline int wait_for_more_data(busfs_reader r, struct fuse_file_info *fi,
                                     busfs_dgram *msg,
                                     uint32_t current_serial, size_t current_size)
{
    int status;
    int ret = 0;

    if (fi->flags & O_NONBLOCK) {
        return -EAGAIN;
    }

    unsigned int waitseq = 42;
    unsigned int start_seq = waitseq;

    pthread_setspecific(_BFG.iowait_seq_key, &waitseq);

    LOG_MSG("Will try and wait for updates... (wait=%lu,start=%lu)",
            waitseq,start_seq);
    status = -1;

    while (waitseq == start_seq && status != 0) {

        status = pthread_mutex_trylock(&r->f->sync.iowait_mutex);
        if (status == -1) {
            assert(errno == EBUSY);
            LOG_MSG("Couldn't acquire lock yet. Sleeping for a second");
            sleep(1);
        }
        LOG_MSG("Acquired mutex!");
    }

    /* Loop has terminated. We have either been aborted by a signal,
     * or we've gotten the lock
     */

    if (status == -1) {
        LOG_MSG("Mutex not yet locked. Returning EBUSY");
        /* Mutex not locked yet */
        ret = -EBUSY;
        goto GT_RET;
    }

    if (waitseq != start_seq) {
        LOG_MSG("Mutex acquired, but we were interrupted");
        pthread_mutex_unlock(&r->f->sync.iowait_mutex);
        ret = -EBUSY;
        goto GT_RET;
    }


    while (waitseq == start_seq &&
            r->f->serial == current_serial &&
            msg->msgsize == current_size) {

        /* Get the timeout for timedwait */
        struct timeval now, offset, result;
        struct timespec timeout;

        gettimeofday(&now, NULL);
        offset.tv_sec = 0;
        offset.tv_usec = 2500; /* 2500 usec */
        busfs_timeval_add(&result, &now, &offset);
        timeout.tv_sec = result.tv_sec;
        timeout.tv_nsec = result.tv_usec * 1000;

        status = pthread_cond_timedwait(&r->f->sync.iowait_cond, &r->f->sync.iowait_mutex,
                &timeout);

        if (waitseq != start_seq) {
            LOG_MSG("Detected interrupt (waitseq=%d, start=%d)", waitseq, start_seq);
            ret = -EBUSY;
            break;
        }
    }
    pthread_mutex_unlock(&r->f->sync.iowait_mutex);

    GT_RET:
    pthread_setspecific(_BFG.iowait_seq_key, NULL);
    LOG_MSG("Returning %d", ret);
    return ret;
}

int busfs_read_file(busfs_reader r, struct fuse_file_info *fi, char *buf, size_t size)
{


    int ret, my_errno = 0;
    int status;

    GT_BEGIN:
    pthread_rwlock_rdlock(&r->f->sync.buf_rwlock);
    busfs_dgram *msg = r->f->dgrams + r->r_idx;
    LOG_MSG("Current index is %d", r->r_idx);
    LOG_MSG("Current serial is %lu", r->r_serial);

    if (msg->serial == r->r_serial && msg->msgsize == r->r_offset) {
        uint32_t current_serial = r->f->serial;
        size_t current_size = msg->msgsize;
        pthread_rwlock_unlock(&r->f->sync.buf_rwlock);
        status = wait_for_more_data(r, fi, msg, current_serial, current_size);
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
