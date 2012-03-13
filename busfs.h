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
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

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

typedef enum {
    BUSFS_GETf_INC = 1 << 0,
    BUSFS_GETf_CREATE = 1 << 1,
    BUSFS_GETf_WRITER = 1 << 2,
} busfs_getflags_t;

typedef enum {
    BUSFS_INFO_NONE,
    BUSFS_INFO_READER = 1,
    BUSFS_INFO_WRITER,
    BUSFS_INFO_CTL
} busfs_info_t;

/* Enum containing various 'flags' */
typedef enum {
    BUSFS_FILESTATE_OK = 0,
    BUSFS_FILESTATE_UNLINKED = 1 << 0,
} busfs_filestate_t;


typedef struct busfs_common_st *busfs_common;
struct busfs_common_st {
    busfs_info_t type;
    int (*read_func)(busfs_common o, const char*, char*, size_t, off_t);
    int (*write_func)(busfs_common o, const char*, const char *, size_t, off_t);
    int (*close_func)(busfs_common o, const char*);
};

typedef struct {
    char *root;
    uint32_t msgsize;
    uint32_t serial;
} busfs_dgram;

struct busfs_file_st {
    /* Common information - Must be first */
    struct busfs_common_st common;


    /* The path */
    char path[FILENAME_MAX];

    /* Structure containing the datagrams themselves */
    busfs_dgram *dgrams;

    /* Maximum length of each datagram */
    size_t dgram_maxlen;

    /* Total count of datagrams in the ringbuffer */
    size_t dgram_count;

    /* Current datagram sequence */
    uint32_t serial;

    /* Index into array of datagrams */
    uint16_t curidx;

    /* Implicit datagram delimiter */
    char delim;

    uint16_t writer_count;
    uint32_t reader_count;

    /* Flag for initialization */
    unsigned initialized :1;

    /* Whether the file has been unlinked */
    unsigned unlinked :1;

    /* Structure containing buffer synchronization variables */
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
    /* Common information. Must be first */
    struct busfs_common_st common;

    /* Flags provided during open() */
    int open_flags;

    /* Serial of the last message read */
    uint32_t r_serial;

    /* Index of the last message read */
    uint16_t r_idx;

    /* Offset into the last message */
    size_t r_offset;

    /* Parent */
    busfs_file f;

};

typedef busfs_file busfs_writer;


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

#define BUSFS_SET_WR(w, fi) \
        fi->fh = (unsigned long)w;

#define BUSFS_GET_WR(fi) \
        (busfs_writer)(fi->fh);

#define BUSFS_GET_COMMON(fi) \
        (busfs_common)(fi->fh);

#define BUSFS_CONVERT_PATH(orig) \
        char BUSFS__converted__ ##orig [FILENAME_MAX]; \
        sprintf(BUSFS__converted__ ## orig, "%s%s", BUSFS_REALFS, orig); \
        orig = BUSFS__converted__ ## orig;

#define BUSFS_CONVERT_PATH_EX(orig, real) \
        const char *real = orig; \
        BUSFS_CONVERT_PATH(real);


void busfs_init(void);

/* File-level functions */

busfs_file busfs_file_get(const char *path, busfs_getflags_t flags);
void busfs_file_release(busfs_file f, busfs_info_t type);

int busfs_file_rename(busfs_file f, const char *to);
int busfs_file_unlink(busfs_file f, const char *path);

/* Reader Funtions */
busfs_reader busfs_read_new(busfs_file f, struct fuse_file_info *fi);
void busfs_read_interrupt_handler(int sig);

/* Writer functions */
busfs_writer busfs_write_new(busfs_file f, struct fuse_file_info *fi);

#endif /*BUSFS_H_*/
