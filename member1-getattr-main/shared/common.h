/*
 * common.h — Shared definitions for Mini-UnionFS
 * All members include this file.
 */

#ifndef MINI_UNIONFS_COMMON_H
#define MINI_UNIONFS_COMMON_H

#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>
#include <limits.h>

/* ── Global State ── */
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

/* ── Helper: build full path from base + relative ── */
static inline void build_full_path(char *dest, const char *base, const char *path) {
    strcpy(dest, base);
    if (path[0] != '/') strcat(dest, "/");
    strcat(dest, path);
}

/* ── Helper: build whiteout path ──
 *  /subdir/foo.txt → upper_dir/subdir/.wh.foo.txt
 */
static inline void build_whiteout_path(char *dest, const char *upper, const char *path) {
    char pathcopy1[PATH_MAX], pathcopy2[PATH_MAX];
    strncpy(pathcopy1, path, PATH_MAX - 1); pathcopy1[PATH_MAX - 1] = '\0';
    strncpy(pathcopy2, path, PATH_MAX - 1); pathcopy2[PATH_MAX - 1] = '\0';

    char *dir  = dirname(pathcopy1);
    char *base = basename(pathcopy2);

    snprintf(dest, PATH_MAX, "%s%s/.wh.%s", upper,
             (strcmp(dir, "/") == 0) ? "" : dir, base);
}

/* ── Helper: check if a whiteout exists ── */
static inline int has_whiteout(const char *path) {
    char wh_path[PATH_MAX];
    build_whiteout_path(wh_path, UNIONFS_DATA->upper_dir, path);
    struct stat st;
    return (lstat(wh_path, &st) == 0);
}

/* ── Helper: resolve which layer a file lives in ──
 *  Returns 0 on success (resolved_path + is_lower set), -ENOENT otherwise.
 */
static inline int resolve_path(const char *path, char *resolved_path, int *is_lower) {
    char upper_path[PATH_MAX], lower_path[PATH_MAX];
    struct stat st;

    if (strcmp(path, "/") == 0) {
        build_full_path(resolved_path, UNIONFS_DATA->upper_dir, path);
        if (is_lower) *is_lower = 0;
        return 0;
    }

    if (has_whiteout(path))
        return -ENOENT;

    build_full_path(upper_path, UNIONFS_DATA->upper_dir, path);
    if (lstat(upper_path, &st) == 0) {
        strcpy(resolved_path, upper_path);
        if (is_lower) *is_lower = 0;
        return 0;
    }

    build_full_path(lower_path, UNIONFS_DATA->lower_dir, path);
    if (lstat(lower_path, &st) == 0) {
        strcpy(resolved_path, lower_path);
        if (is_lower) *is_lower = 1;
        return 0;
    }

    return -ENOENT;
}

/* ── Helper: create parent directories in upper layer ── */
static inline int ensure_parent_dirs(const char *upper_base, const char *rel_path) {
    char full[PATH_MAX];
    snprintf(full, PATH_MAX, "%s%s", upper_base, rel_path);

    char tmp[PATH_MAX];
    strncpy(tmp, full, PATH_MAX - 1); tmp[PATH_MAX - 1] = '\0';

    char *last_slash = strrchr(tmp, '/');
    if (!last_slash || last_slash == tmp) return 0;
    *last_slash = '\0';

    for (char *p = tmp + strlen(upper_base); *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    return 0;
}

/* ── Helper: Copy-on-Write — copy file from lower to upper ── */
static inline int cow_copy(const char *rel_path) {
    char src[PATH_MAX], dst[PATH_MAX];
    build_full_path(src, UNIONFS_DATA->lower_dir, rel_path);
    build_full_path(dst, UNIONFS_DATA->upper_dir, rel_path);

    ensure_parent_dirs(UNIONFS_DATA->upper_dir, rel_path);

    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0) return -errno;

    struct stat st;
    if (fstat(fd_src, &st) != 0) { close(fd_src); return -errno; }

    int fd_dst = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if (fd_dst < 0) { close(fd_src); return -errno; }

    char buffer[8192];
    ssize_t nread;
    while ((nread = read(fd_src, buffer, sizeof(buffer))) > 0) {
        ssize_t nwritten = 0;
        while (nwritten < nread) {
            ssize_t w = write(fd_dst, buffer + nwritten, nread - nwritten);
            if (w < 0) { close(fd_src); close(fd_dst); return -errno; }
            nwritten += w;
        }
    }

    fchmod(fd_dst, st.st_mode);
    close(fd_src);
    close(fd_dst);
    return 0;
}

/* ── Forward declarations of all FUSE callbacks ── */
/* Member 1 */
int unionfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
/* Member 2 */
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags);
int unionfs_mkdir(const char *path, mode_t mode);
int unionfs_rmdir(const char *path);
/* Member 3 */
int unionfs_open(const char *path, struct fuse_file_info *fi);
int unionfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int unionfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int unionfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int unionfs_release(const char *path, struct fuse_file_info *fi);
/* Member 4 */
int unionfs_unlink(const char *path);
int unionfs_truncate(const char *path, off_t size, struct fuse_file_info *fi);
int unionfs_utimens(const char *path, const struct timespec ts[2], struct fuse_file_info *fi);
int unionfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi);
int unionfs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi);
int unionfs_rename(const char *from, const char *to, unsigned int flags);

#endif /* MINI_UNIONFS_COMMON_H */
