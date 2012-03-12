/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall `pkg-config fuse --cflags --libs` fusexmp.c -o fusexmp
*/


#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <stdlib.h>

#include "busfs.h"
#include "busfs_fops.h"


static void *busfs_fuse_init(struct fuse_conn_info *conn)
{
    struct stat sb;
    int ret;

    busfs_log_output = fopen(BUSFS_LOGFILE, "a+");
    if(busfs_log_output == NULL) {
        perror(BUSFS_LOGFILE);
        abort();
    }
    LOG_MSG("HELLO GENTLEMEN!!!");


    LOG_MSG("Initializing...");
    LOG_MSG("Checking if %s exists", BUSFS_REALFS);

    GT_BEGIN:

    ret = stat(BUSFS_REALFS, &sb);
    if (ret == -1) {
        LOG_MSG("It doesn't: (stat: %s)", strerror(errno));
        ret = mkdir(BUSFS_REALFS, 0777);
        if (ret == -1) {
            LOG_MSG("Couldn't create %s: %s", BUSFS_REALFS, strerror(errno));
        } else {
            return NULL;
        }
    } else {
        if (S_ISDIR(sb.st_mode) == 0) {
            unlink(BUSFS_REALFS);
            goto GT_BEGIN;
        }
    }
    busfs_init();
    return NULL;
}

static void busfs_fuse_destroy(void *unused)
{
    LOG_MSG("Destroying filesystem");
}

static struct fuse_operations busfs_ops = {
	.getattr	= busfs_op_getattr,
	.access		= busfs_op_access,
	.readlink	= busfs_op_readlink,
	.readdir	= busfs_op_readdir,
	.mknod		= busfs_op_mknod,
	.mkdir		= busfs_op_mkdir,
	.symlink	= busfs_op_symlink,
	.unlink		= busfs_op_unlink,
	.rmdir		= busfs_op_rmdir,
	.rename		= busfs_op_rename,
	.link		= busfs_op_link,
	.chmod		= busfs_op_chmod,
	.chown		= busfs_op_chown,
	.truncate	= busfs_op_truncate,
	.utimens	= busfs_op_utimens,
	.open		= busfs_op_open,
	.read		= busfs_op_read,
	.write		= busfs_op_write,
	.statfs		= busfs_op_statfs,
	.release	= busfs_op_release,
	.fsync		= busfs_op_fsync,
	.create     = busfs_op_create,

	.init       = busfs_fuse_init,
	.destroy    = busfs_fuse_destroy
#ifdef HAVE_SETXATTR
	.setxattr	= busfs_op_setxattr,
	.getxattr	= busfs_op_getxattr,
	.listxattr	= busfs_op_listxattr,
	.removexattr	= busfs_op_removexattr,
#endif
};

int main(int argc, char *argv[])
{
	umask(0);
	return fuse_main(argc, argv, &busfs_ops, NULL);
}
