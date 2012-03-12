#ifndef BUSFS_FOPS_H_
#define BUSFS_FOPS_H_

#include "busfs.h"

/* boilerplate.c */
int busfs_op_access(const char *path, int mask);
int busfs_op_readlink(const char *path, char *buf, size_t size);
int busfs_op_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi);
int busfs_op_mknod(const char *path, mode_t mode, dev_t rdev);
int busfs_op_mkdir(const char *path, mode_t mode);
int busfs_op_rmdir(const char *path);
int busfs_op_symlink(const char *from, const char *to);
int busfs_op_rename(const char *from, const char *to);
int busfs_op_link(const char *from, const char *to);
int busfs_op_unlink(const char *path);
int busfs_op_chmod(const char *path, mode_t mode);
int busfs_op_chown(const char *path, uid_t uid, gid_t gid);
int busfs_op_truncate(const char *path, off_t size);
int busfs_op_utimens(const char *path, const struct timespec ts[2]);
int busfs_op_fsync(const char *path, int isdatasync,
             struct fuse_file_info *fi);
int busfs_op_statfs(const char *path, struct statvfs *stbuf);

#ifdef HAVE_SETXATTR
int busfs_op_setxattr(const char *path, const char *name, const char *value,
            size_t size, int flags);
int busfs_op_getxattr(const char *path, const char *name, char *value,
            size_t size);
int busfs_op_listxattr(const char *path, char *list, size_t size);
int busfs_op_removexattr(const char *path, const char *name);

#endif /*HAVE_SETXATTR*/

/* fops.c */
int busfs_op_getattr(const char *path, struct stat *stbuf);
int busfs_op_open(const char *path, struct fuse_file_info *fi);
int busfs_op_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi);
int busfs_op_write(const char *path, const char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi);
int busfs_op_release(const char *path, struct fuse_file_info *fi);
int busfs_op_create(const char *path, mode_t mode, struct fuse_file_info *fi);

#endif /*BUSFS_FOPS_H_*/
