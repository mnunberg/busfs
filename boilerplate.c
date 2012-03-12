/**
 * This file contains 'boilerplate' functions which don't do any special handling
 * (for the moment)
 */

#include "busfs.h"

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif


int busfs_op_access(const char *path, int mask)
{
    int res;
    BUSFS_CONVERT_PATH(path);
    res = access(path, mask);
    if (res == -1)
        return -errno;

    return 0;
}

int busfs_op_readlink(const char *path, char *buf, size_t size)
{
    int res;
    BUSFS_CONVERT_PATH(path);
    res = readlink(path, buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}


int busfs_op_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi)
{
    DIR *dp;
    struct dirent *de;
    BUSFS_CONVERT_PATH(path);
    LOG_MSG("Readdir requested on %s", path);
    (void) offset;
    (void) fi;
    dp = opendir(path);
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0))
            break;
    }

    closedir(dp);
    return 0;
}

int busfs_op_mknod(const char *path, mode_t mode, dev_t rdev)
{
    return -EINVAL;
}

int busfs_op_mkdir(const char *path, mode_t mode)
{
    int res;
    BUSFS_CONVERT_PATH(path);
    res = mkdir(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}

int busfs_op_unlink(const char *path)
{
    int res;
    BUSFS_CONVERT_PATH(path);
    res = unlink(path);
    if (res == -1)
        return -errno;

    return 0;
}

int busfs_op_rmdir(const char *path)
{
    int res;
    BUSFS_CONVERT_PATH(path);
    res = rmdir(path);
    if (res == -1)
        return -errno;

    return 0;
}

int busfs_op_symlink(const char *from, const char *to)
{
    int res;
    BUSFS_CONVERT_PATH(from);
    BUSFS_CONVERT_PATH(to);
    res = symlink(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

int busfs_op_rename(const char *from, const char *to)
{
    int res;
    BUSFS_CONVERT_PATH(to);
    res = rename(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

int busfs_op_link(const char *from, const char *to)
{
    int res;
    BUSFS_CONVERT_PATH(from);
    BUSFS_CONVERT_PATH(to);

    res = link(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

int busfs_op_chmod(const char *path, mode_t mode)
{
    int res;
    BUSFS_CONVERT_PATH(path);
    res = chmod(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}

int busfs_op_chown(const char *path, uid_t uid, gid_t gid)
{
    int res;
    BUSFS_CONVERT_PATH(path);
    res = lchown(path, uid, gid);
    if (res == -1)
        return -errno;

    return 0;
}

int busfs_op_truncate(const char *path, off_t size)
{
    int res;
    BUSFS_CONVERT_PATH(path);
    res = truncate(path, size);
    if (res == -1)
        return -errno;

    return 0;
}

int busfs_op_utimens(const char *path, const struct timespec ts[2])
{
    int res;
    BUSFS_CONVERT_PATH(path);
    struct timeval tv[2];
    tv[0].tv_sec = ts[0].tv_sec;
    tv[0].tv_usec = ts[0].tv_nsec / 1000;
    tv[1].tv_sec = ts[1].tv_sec;
    tv[1].tv_usec = ts[1].tv_nsec / 1000;

    res = utimes(path, tv);
    if (res == -1)
        return -errno;

    return 0;
}

int busfs_op_fsync(const char *path, int isdatasync,
             struct fuse_file_info *fi)
{
    /* Just a stub.  This method is optional and can safely be left
       unimplemented */
    BUSFS_CONVERT_PATH(path);
    (void) path;
    (void) isdatasync;
    (void) fi;
    return 0;
}

int busfs_op_statfs(const char *path, struct statvfs *stbuf)
{
    int res;
    BUSFS_CONVERT_PATH(path);
    LOG_MSG("requested stat for %s", path);
    res = statvfs(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}


#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
int busfs_op_setxattr(const char *path, const char *name, const char *value,
            size_t size, int flags)
{
    BUSFS_CONVERT_PATH(path);
    int res = lsetxattr(path, name, value, size, flags);
    if (res == -1)
        return -errno;
    return 0;
}

int busfs_op_getxattr(const char *path, const char *name, char *value,
            size_t size)
{
    BUSFS_CONVERT_PATH(path);
    int res = lgetxattr(path, name, value, size);
    if (res == -1)
        return -errno;
    return res;
}

int busfs_op_listxattr(const char *path, char *list, size_t size)
{
    BUSFS_CONVERT_PATH(path);
    int res = llistxattr(path, list, size);
    if (res == -1)
        return -errno;
    return res;
}

int busfs_op_removexattr(const char *path, const char *name)
{
    BUSFS_CONVERT_PATH(path);
    int res = lremovexattr(path, name);
    if (res == -1)
        return -errno;
    return 0;
}
#endif /* HAVE_SETXATTR */
