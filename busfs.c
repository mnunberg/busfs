#include "busfs.h"
#include <assert.h>
#include <stdlib.h>

#include "busfs_util.h"

struct busfs_global_st BusFS_Global;
FILE *busfs_log_output;

#define _BFG BusFS_Global
#define MINIMUM(a, b) ( ((a) < (b)) ? (a) : (b) )


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
    sa.sa_handler = busfs_read_interrupt_handler;
    sigaction(SIGUSR1, &sa, NULL);

}

/**
 * Just initialize some variables:
 */
static busfs_file new_busfs_file(const char *path)
{
    busfs_file f;
    int ii;
    f = calloc(1, sizeof(struct busfs_file_st));

    f->dgram_maxlen = BUSFS_MSGLEN_INITIAL;
    f->dgram_count = BUSFS_DGRAM_COUNT;
    f->dgrams = calloc(f->dgram_count, sizeof(busfs_dgram));

    for (ii = 0; ii < f->dgram_count; ii++) {
        f->dgrams[ii].root = malloc(f->dgram_maxlen);
    }

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

void busfs_file_release(busfs_file f, busfs_info_t type)
{
    /* Decrement the refcount, and maybe do some other things */
    pthread_rwlock_wrlock(&f->sync.refs_rwlock);
    f->refcount--;

    switch(type) {
    case BUSFS_INFO_READER:
        f->reader_count--;
        break;
    case BUSFS_INFO_WRITER:
        f->writer_count--;
        break;
    default:
        break;
    }

    if (f->refcount == 0 && f->unlinked) {
        size_t ii;
        for (ii = 0; ii < f->dgram_count; ii++) {
            busfs_dgram *msg = f->dgrams + ii;
            free(msg->root);
        }
        free(f->dgrams);
        pthread_rwlock_destroy(&f->sync.refs_rwlock);
        pthread_rwlock_destroy(&f->sync.buf_rwlock);
        pthread_mutex_destroy(&f->sync.iowait_mutex);
        pthread_cond_destroy(&f->sync.iowait_cond);
        free(f);
    } else {
        pthread_rwlock_unlock(&f->sync.refs_rwlock);
    }
}

busfs_file busfs_file_get(const char *path, busfs_getflags_t flags)
{
    busfs_file f;
    int ret;
    LOG_MSG("Requested file object for %s", path);

    ret = pthread_rwlock_rdlock(&_BFG.lock);

    if (ret != 0) {
        LOG_MSG("Couldn't lock: %s", strerror(ret));
        return NULL;
    }

    LOG_MSG("Locked hashtable via rdlock");


    f = (busfs_file)g_hash_table_lookup(_BFG.ht, path);

    LOG_MSG("Initial lookup returns %p", f);

    if (f && (flags & BUSFS_GETf_INC)) {
        LOG_MSG("Incrementing the refcount");
        pthread_rwlock_wrlock(&f->sync.refs_rwlock);
        f->refcount++;
        pthread_rwlock_unlock(&f->sync.refs_rwlock);
    }

    pthread_rwlock_unlock(&_BFG.lock);

    if (f) {
        return f;
    } else {
        if ((flags & BUSFS_GETf_CREATE) == 0) {
            return NULL;
        }

        ret = pthread_rwlock_wrlock(&_BFG.lock);
        if (ret != 0) {
            return NULL;
        }

        f = new_busfs_file(path);
        g_hash_table_insert(_BFG.ht, f->path, f);

        f->refcount = (flags & BUSFS_GETf_INC) ? 1 : 0;

        pthread_rwlock_unlock(&_BFG.lock);
        return f;
    }
}

int busfs_file_rename(busfs_file f, const char *to)
{
    const char *from = f->path;
    BUSFS_CONVERT_PATH(to);
    BUSFS_CONVERT_PATH(from);

    pthread_rwlock_wrlock(&_BFG.lock);
    g_hash_table_remove(_BFG.ht, from);
    strncpy(f->path, to, sizeof(f->path));
    g_hash_table_insert(_BFG.ht, f->path, f);

    pthread_rwlock_unlock(&_BFG.lock);
    return 0;
}

int busfs_file_unlink(busfs_file f, const char *path)
{
    BUSFS_CONVERT_PATH(path);

    pthread_rwlock_wrlock(&_BFG.lock);
    g_hash_table_remove(_BFG.ht, f->path);
    pthread_rwlock_unlock(&_BFG.lock);
    f->unlinked = 1;
    return 0;
}
