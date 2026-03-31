/*
 * member4.c — Member 4: unlink, truncate, utimens, chmod, chown, rename
 *
 * Responsibility:
 *   - unlink   (delete files; create whiteout if file is in lower)
 *   - truncate (CoW + truncate)
 *   - utimens  (CoW + update timestamps)
 *   - chmod    (CoW + change permissions)
 *   - chown    (CoW + change ownership)
 *   - rename   (CoW source if needed; move in upper; whiteout old name)
 *
 * Shared helpers you rely on (defined in common.h):
 *   resolve_path(), build_full_path(), build_whiteout_path(),
 *   cow_copy(), ensure_parent_dirs(), UNIONFS_DATA
 */

#include "../shared/common.h"

/* ── unlink ──
 * The whiteout mechanism.
 * - If the file is only in upper → just delete it normally.
 * - If the file is in lower (or both) → create a .wh. marker in upper
 *   so that resolve_path() hides the lower copy.
 */
int unionfs_unlink(const char *path) {
    char upper_path[PATH_MAX], lower_path[PATH_MAX];
    struct stat st;

    build_full_path(upper_path, UNIONFS_DATA->upper_dir, path);
    build_full_path(lower_path, UNIONFS_DATA->lower_dir, path);

    int in_upper = (lstat(upper_path, &st) == 0);
    int in_lower = (lstat(lower_path, &st) == 0);

    /* Remove the upper copy if it exists */
    if (in_upper) {
        if (unlink(upper_path) != 0)
            return -errno;
    }

    /* If it existed in lower, create a whiteout to hide it */
    if (in_lower) {
        char wh_path[PATH_MAX];
        build_whiteout_path(wh_path, UNIONFS_DATA->upper_dir, path);

        /* Ensure parent dir in upper exists (for nested paths) */
        ensure_parent_dirs(UNIONFS_DATA->upper_dir, path);

        int fd = creat(wh_path, 0644);
        if (fd < 0) return -errno;
        close(fd);
    }

    return 0;
}

/* ── truncate ──
 * If the file is in lower, CoW it first, then truncate the upper copy.
 */
int unionfs_truncate(const char *path, off_t size,
                     struct fuse_file_info *fi) {
    (void) fi;
    char resolved[PATH_MAX];
    int is_lower = 0;

    int res = resolve_path(path, resolved, &is_lower);
    if (res != 0) return res;

    if (is_lower) {
        int cow_res = cow_copy(path);
        if (cow_res != 0) return cow_res;
        build_full_path(resolved, UNIONFS_DATA->upper_dir, path);
    }

    if (truncate(resolved, size) != 0)
        return -errno;

    return 0;
}

/* ── utimens ──
 * Update file timestamps. CoW if file is in lower.
 */
int unionfs_utimens(const char *path, const struct timespec ts[2],
                    struct fuse_file_info *fi) {
    (void) fi;
    char resolved[PATH_MAX];
    int is_lower = 0;

    int res = resolve_path(path, resolved, &is_lower);
    if (res != 0) return res;

    if (is_lower) {
        int cow_res = cow_copy(path);
        if (cow_res != 0) return cow_res;
        build_full_path(resolved, UNIONFS_DATA->upper_dir, path);
    }

    struct timespec times[2] = { ts[0], ts[1] };
    if (utimensat(AT_FDCWD, resolved, times, AT_SYMLINK_NOFOLLOW) != 0)
        return -errno;

    return 0;
}

/* ── chmod ──
 * Change permissions. CoW if file is in lower.
 */
int unionfs_chmod(const char *path, mode_t mode,
                  struct fuse_file_info *fi) {
    (void) fi;
    char resolved[PATH_MAX];
    int is_lower = 0;

    int res = resolve_path(path, resolved, &is_lower);
    if (res != 0) return res;

    if (is_lower) {
        int cow_res = cow_copy(path);
        if (cow_res != 0) return cow_res;
        build_full_path(resolved, UNIONFS_DATA->upper_dir, path);
    }

    if (chmod(resolved, mode) != 0)
        return -errno;

    return 0;
}

/* ── chown ──
 * Change ownership. CoW if file is in lower.
 */
int unionfs_chown(const char *path, uid_t uid, gid_t gid,
                  struct fuse_file_info *fi) {
    (void) fi;
    char resolved[PATH_MAX];
    int is_lower = 0;

    int res = resolve_path(path, resolved, &is_lower);
    if (res != 0) return res;

    if (is_lower) {
        int cow_res = cow_copy(path);
        if (cow_res != 0) return cow_res;
        build_full_path(resolved, UNIONFS_DATA->upper_dir, path);
    }

    if (lchown(resolved, uid, gid) != 0)
        return -errno;

    return 0;
}

/* ── rename ──
 * Move/rename a file within the union.
 * - CoW the source if it's in lower.
 * - Move within upper layer.
 * - Create whiteout for old name if it existed in lower.
 * - Remove whiteout at destination if any.
 */
int unionfs_rename(const char *from, const char *to, unsigned int flags) {
    (void) flags;
    char resolved_from[PATH_MAX];
    int is_lower_from = 0;

    int res = resolve_path(from, resolved_from, &is_lower_from);
    if (res != 0) return res;

    /* CoW source if it lives in lower */
    if (is_lower_from) {
        int cow_res = cow_copy(from);
        if (cow_res != 0) return cow_res;
        build_full_path(resolved_from, UNIONFS_DATA->upper_dir, from);
    }

    /* Destination always goes to upper */
    char upper_to[PATH_MAX];
    build_full_path(upper_to, UNIONFS_DATA->upper_dir, to);
    ensure_parent_dirs(UNIONFS_DATA->upper_dir, to);

    /* Remove whiteout at destination */
    char wh_to[PATH_MAX];
    build_whiteout_path(wh_to, UNIONFS_DATA->upper_dir, to);
    unlink(wh_to);

    if (rename(resolved_from, upper_to) != 0)
        return -errno;

    /* Whiteout old name if it existed in lower */
    char lower_from[PATH_MAX];
    struct stat st;
    build_full_path(lower_from, UNIONFS_DATA->lower_dir, from);
    if (lstat(lower_from, &st) == 0) {
        char wh_from[PATH_MAX];
        build_whiteout_path(wh_from, UNIONFS_DATA->upper_dir, from);
        int fd = creat(wh_from, 0644);
        if (fd >= 0) close(fd);
    }

    return 0;
}
