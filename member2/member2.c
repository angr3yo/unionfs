/*
 * member2.c — Member 2: readdir, mkdir, rmdir
 *
 * Responsibility:
 *   - readdir  (merge entries from upper + lower, hide whiteouts)
 *   - mkdir    (create directories in upper layer)
 *   - rmdir    (remove dirs; create whiteout if dir exists in lower)
 *
 * Shared helpers you rely on (defined in common.h):
 *   build_full_path(), build_whiteout_path(), UNIONFS_DATA
 */

#include "../shared/common.h"

/* ── readdir ──
 * Merges directory listings from both layers.
 * - Upper entries are listed first.
 * - .wh.* entries are collected but NOT shown (they are whiteout markers).
 * - Lower entries are shown only if they are NOT already in upper
 *   AND NOT hidden by a whiteout.
 */
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    /* Track names we've already added (to avoid duplicates) */
    char seen[4096][256];
    int seen_count = 0;

    /* Track whiteout targets (filenames hidden by .wh. markers) */
    char whiteouts[4096][256];
    int wh_count = 0;

    /* ── Pass 1: Upper directory ── */
    char upper_path[PATH_MAX];
    build_full_path(upper_path, UNIONFS_DATA->upper_dir, path);
    DIR *dp = opendir(upper_path);
    if (dp) {
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;

            /* Whiteout marker? Record the target name but don't list it */
            if (strncmp(de->d_name, ".wh.", 4) == 0) {
                strncpy(whiteouts[wh_count], de->d_name + 4, 255);
                whiteouts[wh_count][255] = '\0';
                wh_count++;
                continue;
            }

            /* Normal upper entry — add to listing */
            filler(buf, de->d_name, NULL, 0, 0);
            strncpy(seen[seen_count], de->d_name, 255);
            seen[seen_count][255] = '\0';
            seen_count++;
        }
        closedir(dp);
    }

    /* ── Pass 2: Lower directory ── */
    char lower_path[PATH_MAX];
    build_full_path(lower_path, UNIONFS_DATA->lower_dir, path);
    dp = opendir(lower_path);
    if (dp) {
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;

            /* Skip if already seen in upper (upper takes precedence) */
            int skip = 0;
            for (int i = 0; i < seen_count; i++) {
                if (strcmp(seen[i], de->d_name) == 0) { skip = 1; break; }
            }
            if (skip) continue;

            /* Skip if a whiteout hides this file */
            for (int i = 0; i < wh_count; i++) {
                if (strcmp(whiteouts[i], de->d_name) == 0) { skip = 1; break; }
            }
            if (skip) continue;

            /* Lower entry visible — add to listing */
            filler(buf, de->d_name, NULL, 0, 0);
        }
        closedir(dp);
    }

    return 0;
}

/* ── mkdir ──
 * Always creates directories in the upper layer.
 * If a whiteout exists for this directory name, remove it first
 * (allows re-creation of a previously deleted directory).
 */
int unionfs_mkdir(const char *path, mode_t mode) {
    char upper_path[PATH_MAX];
    build_full_path(upper_path, UNIONFS_DATA->upper_dir, path);

    /* Remove any existing whiteout */
    char wh_path[PATH_MAX];
    build_whiteout_path(wh_path, UNIONFS_DATA->upper_dir, path);
    unlink(wh_path);

    if (mkdir(upper_path, mode) != 0)
        return -errno;

    return 0;
}

/* ── rmdir ──
 * - If dir exists in upper → remove it.
 * - If dir exists in lower → create a whiteout to hide it.
 */
int unionfs_rmdir(const char *path) {
    char upper_path[PATH_MAX], lower_path[PATH_MAX];
    struct stat st;

    build_full_path(upper_path, UNIONFS_DATA->upper_dir, path);
    build_full_path(lower_path, UNIONFS_DATA->lower_dir, path);

    int in_upper = (lstat(upper_path, &st) == 0);
    int in_lower = (lstat(lower_path, &st) == 0);

    if (in_upper) {
        if (rmdir(upper_path) != 0)
            return -errno;
    }

    if (in_lower) {
        char wh_path[PATH_MAX];
        build_whiteout_path(wh_path, UNIONFS_DATA->upper_dir, path);
        int fd = creat(wh_path, 0644);
        if (fd < 0) return -errno;
        close(fd);
    }

    return 0;
}
