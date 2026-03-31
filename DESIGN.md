# Mini-UnionFS: Design Document

## 1. Architecture Overview

Mini-UnionFS is a userspace filesystem built on FUSE3 that presents a unified view of two directory layers:

- **Lower layer** (`lower_dir`): Read-only base image, never modified by the filesystem.
- **Upper layer** (`upper_dir`): Read-write container layer where all modifications are stored.

When the filesystem is mounted, users see a single merged directory tree. The upper layer takes precedence over the lower layer, meaning if a file exists in both, the upper version is shown.

```
 User Applications (ls, cat, echo, rm, ...)
              |
        [VFS / Kernel]
              |
        [FUSE driver]
              |
    [mini_unionfs process]
         /         \
   lower_dir     upper_dir
   (read-only)   (read-write)
```

## 2. Key Data Structures

### Global State (`mini_unionfs_state`)
Stores absolute paths to the lower and upper directories. Passed to all FUSE callbacks via `fuse_get_context()->private_data`.

### Path Resolution (`resolve_path`)
The central function called by every operation. Given a relative path like `/config.txt`, it:
1. Checks if `upper_dir/.wh.config.txt` exists → return ENOENT (file is whiteouted/deleted)
2. Checks if `upper_dir/config.txt` exists → return this path (upper takes precedence)
3. Checks if `lower_dir/config.txt` exists → return this path (fallback to lower)
4. Returns -ENOENT if not found anywhere

The function also outputs an `is_lower` flag so callers know whether Copy-on-Write is needed.

### Whiteout Files
A file named `.wh.<original_filename>` placed in the upper directory signals that the corresponding lower-layer file should be hidden. For nested paths like `/subdir/foo.txt`, the whiteout is placed at `upper_dir/subdir/.wh.foo.txt`.

## 3. Copy-on-Write (CoW) Strategy

When a user opens a lower-layer file for writing (`O_WRONLY`, `O_RDWR`, `O_APPEND`, or `O_TRUNC`), the filesystem:
1. Creates any necessary parent directories in the upper layer
2. Copies the entire file from lower to upper byte-by-byte
3. Preserves the original file permissions via `fchmod()`
4. Redirects the open() call to the new upper-layer copy

The lower-layer file is never touched. This also applies to `truncate`, `chmod`, `chown`, and `utimens` — any metadata-modifying operation triggers CoW first.

## 4. Operation Details

| Operation | Behavior |
|-----------|----------|
| `getattr` | Resolve path, call `lstat()` on the resolved location |
| `readdir` | Merge entries from upper and lower; skip `.wh.*` files; skip lower entries that have whiteouts |
| `open`    | Resolve path; if lower + write flags → CoW copy then open upper copy |
| `read`    | `pread()` on the file descriptor stored in `fi->fh` |
| `write`   | `pwrite()` on the file descriptor stored in `fi->fh` |
| `create`  | Always in upper; removes existing whiteout if present |
| `unlink`  | If in upper: physical delete. If in lower: create `.wh.` marker. If both: delete upper + create whiteout |
| `mkdir`   | Always in upper; removes existing whiteout |
| `rmdir`   | Remove from upper if present; create whiteout if exists in lower |
| `rename`  | CoW source if lower; move in upper; create whiteout for old name if it existed in lower |
| `truncate`| CoW if lower, then truncate the upper copy |

## 5. Edge Cases Handled

1. **Root path** (`/`): Always valid — getattr returns stats from upper root, readdir merges both.
2. **File exists in both layers**: Upper always wins (checked first in `resolve_path`).
3. **Re-creation after deletion**: `create` removes the `.wh.` file, allowing the filename to be reused.
4. **Nested directory CoW**: `ensure_parent_dirs()` creates intermediate directories in upper before copying.
5. **Nested whiteouts**: Whiteout files are placed alongside the file in the correct subdirectory of upper.
6. **Append to lower file**: Triggers full CoW (preserving existing content) before the append write.
7. **Concurrent reads**: Multiple reads from lower are safe since it's read-only.

## 6. Build & Test Instructions

```bash
# Install dependencies (Ubuntu 22.04)
sudo apt install -y build-essential libfuse3-dev fuse3 pkg-config

# Build
make

# Run tests
make test

# Manual mount
mkdir -p /tmp/lower /tmp/upper /tmp/mnt
echo "hello" > /tmp/lower/test.txt
./mini_unionfs /tmp/lower /tmp/upper /tmp/mnt -f

# (In another terminal)
cat /tmp/mnt/test.txt       # reads from lower
echo "world" >> /tmp/mnt/test.txt  # triggers CoW, writes to upper
cat /tmp/upper/test.txt     # contains "hello\nworld"
cat /tmp/lower/test.txt     # still just "hello"

# Unmount
fusermount -u /tmp/mnt
```
