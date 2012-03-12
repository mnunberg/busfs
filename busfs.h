#ifndef BUSFS_H_
#define BUSFS_H_

#define FUSE_USE_VERSION 26


#include <glib.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdint.h>
#include <fuse.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <utime.h>
#include <sys/time.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*Where to log output*/

#ifndef BUSFS_LOGFILE
#define BUSFS_LOGFILE "busfs.log"
#endif /* BUSFS_LOGFILE */

/*The 'real' path which takes care of the fs map */
#ifndef BUSFS_REALFS
#define BUSFS_REALFS "/tmp/busfs"
#endif /*BUSFS_REALFS*/

#define BUSFS_DGRAM_COUNT 1024
#define BUSFS_MSGLEN_INITIAL 256

typedef struct busfs_file_st* busfs_file;

typedef struct {

    char root[BUSFS_MSGLEN_INITIAL];

    uint32_t msgsize;
    uint32_t serial;
} busfs_dgram;

struct busfs_file_st {
    /* The path */
    char path[FILENAME_MAX];

    busfs_dgram dgrams[BUSFS_DGRAM_COUNT];
    uint32_t serial;

    uint16_t curidx;

    char delim;

    /* Whether we have a writer */
    char has_writer;

    struct {
        /* Lock controlling buffer positions and indices */
        pthread_rwlock_t buf_rwlock;

        /* condvar + mutex for new data */
        pthread_cond_t iowait_cond;
        pthread_mutex_t iowait_mutex;

        /* lock controlling the manipulation of refcounts */
        pthread_rwlock_t refs_rwlock;

    } sync;

    /* Total number of 'filehandles' */
    uint32_t refcount;

    /* 'time' for update */
    time_t mtime;
};

/* Structure defining a 'reader' */
typedef struct busfs_reader_st* busfs_reader;
struct busfs_reader_st {
    /* Serial of the last message read */
    uint32_t r_serial;

    /* Index of the last message read */
    uint16_t r_idx;

    /* Offset into the last message */
    size_t r_offset;

    /* Parent */
    busfs_file f;

};

struct busfs_global_st {
    pthread_rwlock_t lock;

    /* key for pthread_getspecific, used for aborting iowait lock loops */
    pthread_key_t iowait_seq_key;
    GHashTable *ht;
};

extern struct busfs_global_st BusFS_Global;
extern FILE *busfs_log_output;


#define LOG_MSG(...) \
    fprintf(busfs_log_output, "BUSFS[%s:%d] ", __func__, __LINE__); \
    fprintf(busfs_log_output, __VA_ARGS__); \
    fprintf(busfs_log_output, "\n"); \
    fflush(busfs_log_output);





#define BUSFS_SET_FI(st,fi) \
    fi->fh = (unsigned long)st

#define BUSFS_GET_FI(fi) \
    (busfs_file)(fi->fh)

#define BUSFS_GET_RDR(fi) \
    (busfs_reader)(fi->fh)

#define BUSFS_SET_RDR(r, fi) \
    fi->fh = (unsigned long)r;

#define BUSFS_CONVERT_PATH(orig) \
        char BUSFS__converted__ ##orig [FILENAME_MAX]; \
        sprintf(BUSFS__converted__ ## orig, "%s%s", BUSFS_REALFS, orig); \
        orig = BUSFS__converted__ ## orig;


void busfs_init(void);

busfs_file busfs_file_get(const char *path, int create, int inc);

int busfs_file_write(busfs_file f, const char *buf, size_t size);

busfs_reader busfs_reader_new(busfs_file f);
void busfs_reader_free(busfs_reader r);

int busfs_read_file(busfs_reader r, struct fuse_file_info *fi, char *buf, size_t size);



#endif /*BUSFS_H_*/
