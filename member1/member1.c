/*
 * member1.c — Member 1: getattr + main() + FUSE operations table
 *
 * Responsibility:
 *   - getattr (stat any file/directory in the union)
 *   - main()  (parse args, set up state, launch FUSE)
 *   - fuse_operations struct (wires everyone's functions together)
 *
 * Shared helpers you rely on (defined in common.h):
 *   resolve_path(), build_full_path(), UNIONFS_DATA
 */

#include "../shared/common.h"

/* ── getattr ──
 * Called by the kernel for every stat(), ls, access check, etc.
 * Resolves which layer the file is in, then calls lstat().
 */
int unionfs_getattr(const char *path, struct stat *stbuf,
                    struct fuse_file_info *fi) {
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    /* Root directory: return stats from upper (or lower as fallback) */
    if (strcmp(path, "/") == 0) {
        char upper_root[PATH_MAX];
        build_full_path(upper_root, UNIONFS_DATA->upper_dir, "/");
        if (lstat(upper_root, stbuf) != 0) {
            char lower_root[PATH_MAX];
            build_full_path(lower_root, UNIONFS_DATA->lower_dir, "/");
            if (lstat(lower_root, stbuf) != 0)
                return -ENOENT;
        }
        return 0;
    }

    /* Normal file/directory: resolve across layers */
    char resolved[PATH_MAX];
    int is_lower;
    int res = resolve_path(path, resolved, &is_lower);
    if (res != 0) return res;

    if (lstat(resolved, stbuf) != 0)
        return -errno;

    return 0;
}

/* ── FUSE Operations Table ──
 * Wires every member's callback into the FUSE framework.
 */
static struct fuse_operations unionfs_oper = {
    /* Member 1 */
    .getattr  = unionfs_getattr,
    /* Member 2 */
    .readdir  = unionfs_readdir,
    .mkdir    = unionfs_mkdir,
    .rmdir    = unionfs_rmdir,
    /* Member 3 */
    .open     = unionfs_open,
    .read     = unionfs_read,
    .write    = unionfs_write,
    .create   = unionfs_create,
    .release  = unionfs_release,
    /* Member 4 */
    .unlink   = unionfs_unlink,
    .truncate = unionfs_truncate,
    .utimens  = unionfs_utimens,
    .chmod    = unionfs_chmod,
    .chown    = unionfs_chown,
    .rename   = unionfs_rename,
};

/* ── main() ──
 * Usage: ./mini_unionfs <lower_dir> <upper_dir> <mount_point> [-f]
 *   -f = foreground mode (useful for debugging, shows printf output)
 */
int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mount_point> [fuse_options]\n",
                argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = malloc(sizeof(struct mini_unionfs_state));
    if (!state) { perror("malloc"); return 1; }

    /* Convert to absolute paths so FUSE doesn't break after chdir */
    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);

    if (!state->lower_dir || !state->upper_dir) {
        fprintf(stderr, "Error: lower_dir or upper_dir does not exist.\n");
        return 1;
    }

    printf("Mini-UnionFS starting...\n");
    printf("  Lower (read-only):  %s\n", state->lower_dir);
    printf("  Upper (read-write): %s\n", state->upper_dir);
    printf("  Mount point:        %s\n", argv[3]);

    /* Rebuild argv for FUSE: [program_name, mount_point, ...extra_opts] */
    int fuse_argc = argc - 2;
    char **fuse_argv = malloc(sizeof(char *) * fuse_argc);
    fuse_argv[0] = argv[0];
    fuse_argv[1] = argv[3];
    for (int i = 4; i < argc; i++)
        fuse_argv[i - 2] = argv[i];

    int ret = fuse_main(fuse_argc, fuse_argv, &unionfs_oper, state);

    free(state->lower_dir);
    free(state->upper_dir);
    free(state);
    free(fuse_argv);

    return ret;
}
