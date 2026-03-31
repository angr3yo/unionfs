/*
 * member3.c — Member 3: open, read, write, create, release
 *
 * Responsibility:
 *   - open     (detect writes to lower files → trigger Copy-on-Write)
 *   - read     (pread from the file descriptor)
 *   - write    (pwrite to the file descriptor)
 *   - create   (new files always go in upper; clear whiteout if needed)
 *   - release  (close the file descriptor)
 *
 * Shared helpers you rely on (defined in common.h):
 *   resolve_path(), build_full_path(), build_whiteout_path(),
 *   cow_copy(), ensure_parent_dirs(), UNIONFS_DATA
 */

#include "../shared/common.h"

/* ── open ──
 * The CoW trigger point.
 * If a file lives in the lower layer and the user opens it for writing,
 * we copy the entire file to upper FIRST, then open the upper copy.
 * This guarantees the lower layer is never modified.
 */
int unionfs_open(const char *path, struct fuse_file_info *fi) {
    char resolved[PATH_MAX];
    int is_lower = 0;

    int res = resolve_path(path, resolved, &is_lower);
    if (res != 0) return res;

    /* Check if opening a lower-layer file for writing */
    if (is_lower && (fi->flags & (O_WRONLY | O_RDWR | O_APPEND | O_TRUNC))) {
        /* Copy-on-Write: duplicate the file to upper layer */
        int cow_res = cow_copy(path);
        if (cow_res != 0) return cow_res;

        /* Now point to the upper copy */
        build_full_path(resolved, UNIONFS_DATA->upper_dir, path);
    }

    /* Open the resolved file and store the fd for read/write */
    int fd = open(resolved, fi->flags);
    if (fd < 0) return -errno;

    fi->fh = fd;  /* FUSE carries this fd through to read/write/release */
    return 0;
}

/* ── read ──
 * Straightforward: pread from the fd that open() stored in fi->fh.
 * pread is used (not read) because FUSE may request reads at any offset.
 */
int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                 struct fuse_file_info *fi) {
    (void) path;
    ssize_t res = pread(fi->fh, buf, size, offset);
    if (res < 0) return -errno;
    return res;
}

/* ── write ──
 * Straightforward: pwrite to the fd.
 * By the time write() is called, open() has already ensured the fd
 * points to an upper-layer file (via CoW if necessary).
 */
int unionfs_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi) {
    (void) path;
    ssize_t res = pwrite(fi->fh, buf, size, offset);
    if (res < 0) return -errno;
    return res;
}

/* ── create ──
 * Creates a brand-new file. Always placed in the upper layer.
 * If a whiteout existed for this filename (i.e., the file was previously
 * deleted), the whiteout is removed so the file becomes visible again.
 */
int unionfs_create(const char *path, mode_t mode,
                   struct fuse_file_info *fi) {
    char upper_path[PATH_MAX];
    build_full_path(upper_path, UNIONFS_DATA->upper_dir, path);

    /* Make sure parent dirs exist in upper */
    ensure_parent_dirs(UNIONFS_DATA->upper_dir, path);

    /* Remove any whiteout (allows re-creation of deleted files) */
    char wh_path[PATH_MAX];
    build_whiteout_path(wh_path, UNIONFS_DATA->upper_dir, path);
    unlink(wh_path);

    int fd = open(upper_path, fi->flags | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return -errno;

    fi->fh = fd;
    return 0;
}

/* ── release ──
 * Called when a file is closed. Just close the fd.
 */
int unionfs_release(const char *path, struct fuse_file_info *fi) {
    (void) path;
    close(fi->fh);
    return 0;
}
