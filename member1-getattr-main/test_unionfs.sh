#!/bin/bash
# ============================================================
#  Mini-UnionFS Automated Test Suite
# ============================================================

FUSE_BINARY="./mini_unionfs"
TEST_DIR="./unionfs_test_env"
LOWER_DIR="$TEST_DIR/lower"
UPPER_DIR="$TEST_DIR/upper"
MOUNT_DIR="$TEST_DIR/mnt"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

PASS=0
FAIL=0

pass() { echo -e "  ${GREEN}PASSED${NC}"; ((PASS++)); }
fail() { echo -e "  ${RED}FAILED${NC} - $1"; ((FAIL++)); }

echo "============================================"
echo " Mini-UnionFS Test Suite"
echo "============================================"
echo ""

# ----- SETUP -----
echo -e "${YELLOW}Setting up test environment...${NC}"
rm -rf "$TEST_DIR"
mkdir -p "$LOWER_DIR/subdir" "$UPPER_DIR" "$MOUNT_DIR"

# Populate lower (read-only base) layer
echo "base_only_content" > "$LOWER_DIR/base.txt"
echo "to_be_deleted" > "$LOWER_DIR/delete_me.txt"
echo "lower_version" > "$LOWER_DIR/override.txt"
echo "nested_content" > "$LOWER_DIR/subdir/nested.txt"
echo "lower_sub_delete" > "$LOWER_DIR/subdir/sub_delete.txt"

# Make lower read-only to verify CoW doesn't touch it
# (We don't chmod because FUSE runs as user; we verify by content check instead)

# Populate upper layer with an override
echo "upper_version" > "$UPPER_DIR/override.txt"

# Mount
$FUSE_BINARY "$LOWER_DIR" "$UPPER_DIR" "$MOUNT_DIR" -f &
FUSE_PID=$!
sleep 2

# Verify mount succeeded
if ! mountpoint -q "$MOUNT_DIR" 2>/dev/null && ! ls "$MOUNT_DIR/base.txt" &>/dev/null; then
    echo -e "${RED}FATAL: Mount failed. Aborting tests.${NC}"
    kill $FUSE_PID 2>/dev/null
    rm -rf "$TEST_DIR"
    exit 1
fi

echo ""
echo "--- Core Tests ---"

# ----- TEST 1: Layer Visibility -----
echo -n "Test 1: Lower-layer file visible in mount..."
if grep -q "base_only_content" "$MOUNT_DIR/base.txt" 2>/dev/null; then
    pass
else
    fail "base.txt not visible or wrong content"
fi

# ----- TEST 2: Upper Override -----
echo -n "Test 2: Upper layer overrides lower layer..."
if grep -q "upper_version" "$MOUNT_DIR/override.txt" 2>/dev/null; then
    pass
else
    fail "override.txt should show upper_version"
fi

# ----- TEST 3: Merged Directory Listing -----
echo -n "Test 3: Merged directory listing (ls)..."
LISTING=$(ls "$MOUNT_DIR" 2>/dev/null)
if echo "$LISTING" | grep -q "base.txt" && \
   echo "$LISTING" | grep -q "delete_me.txt" && \
   echo "$LISTING" | grep -q "override.txt"; then
    pass
else
    fail "ls should show files from both layers"
fi

# ----- TEST 4: Copy-on-Write -----
echo -n "Test 4: Copy-on-Write (modify lower file)..."
echo "modified_content" >> "$MOUNT_DIR/base.txt" 2>/dev/null
sleep 0.5
MOUNT_HAS=$(grep -c "modified_content" "$MOUNT_DIR/base.txt" 2>/dev/null)
UPPER_HAS=$(grep -c "modified_content" "$UPPER_DIR/base.txt" 2>/dev/null)
LOWER_CLEAN=$(grep -c "modified_content" "$LOWER_DIR/base.txt" 2>/dev/null)
if [ "$MOUNT_HAS" -eq 1 ] && [ "$UPPER_HAS" -eq 1 ] && [ "$LOWER_CLEAN" -eq 0 ]; then
    pass
else
    fail "mount=$MOUNT_HAS upper=$UPPER_HAS lower_modified=$LOWER_CLEAN"
fi

# ----- TEST 5: Whiteout (Delete lower file) -----
echo -n "Test 5: Whiteout mechanism (delete lower file)..."
rm "$MOUNT_DIR/delete_me.txt" 2>/dev/null
sleep 0.5
if [ ! -e "$MOUNT_DIR/delete_me.txt" ] && \
   [ -f "$LOWER_DIR/delete_me.txt" ] && \
   [ -f "$UPPER_DIR/.wh.delete_me.txt" ]; then
    pass
else
    fail "File should be hidden, lower intact, whiteout created"
fi

# ----- TEST 6: Deleted file hidden from ls -----
echo -n "Test 6: Deleted file hidden from directory listing..."
if ! ls "$MOUNT_DIR" 2>/dev/null | grep -q "delete_me.txt"; then
    pass
else
    fail "delete_me.txt should not appear in ls"
fi

# ----- TEST 7: Create new file -----
echo -n "Test 7: Create new file in mount..."
echo "new_file_content" > "$MOUNT_DIR/new_file.txt" 2>/dev/null
sleep 0.5
if [ -f "$MOUNT_DIR/new_file.txt" ] && \
   [ -f "$UPPER_DIR/new_file.txt" ] && \
   [ ! -f "$LOWER_DIR/new_file.txt" ]; then
    pass
else
    fail "New file should exist in mount and upper only"
fi

# ----- TEST 8: Delete and re-create -----
echo -n "Test 8: Delete then re-create a file..."
rm "$MOUNT_DIR/new_file.txt" 2>/dev/null
sleep 0.3
echo "recreated" > "$MOUNT_DIR/new_file.txt" 2>/dev/null
sleep 0.5
if grep -q "recreated" "$MOUNT_DIR/new_file.txt" 2>/dev/null && \
   [ ! -f "$UPPER_DIR/.wh.new_file.txt" ]; then
    pass
else
    fail "Re-created file should work, whiteout should be removed"
fi

# ----- TEST 9: Nested directory visibility -----
echo -n "Test 9: Nested directory/file from lower layer..."
if grep -q "nested_content" "$MOUNT_DIR/subdir/nested.txt" 2>/dev/null; then
    pass
else
    fail "subdir/nested.txt should be visible"
fi

# ----- TEST 10: mkdir in mount -----
echo -n "Test 10: Create directory in mount..."
mkdir "$MOUNT_DIR/newdir" 2>/dev/null
sleep 0.3
if [ -d "$MOUNT_DIR/newdir" ] && [ -d "$UPPER_DIR/newdir" ]; then
    pass
else
    fail "newdir should exist in mount and upper"
fi

# ----- TEST 11: Nested whiteout -----
echo -n "Test 11: Delete file in nested directory..."
rm "$MOUNT_DIR/subdir/sub_delete.txt" 2>/dev/null
sleep 0.5
if [ ! -e "$MOUNT_DIR/subdir/sub_delete.txt" ] && \
   [ -f "$LOWER_DIR/subdir/sub_delete.txt" ] && \
   [ -f "$UPPER_DIR/subdir/.wh.sub_delete.txt" ]; then
    pass
else
    fail "Nested whiteout should work"
fi

# ----- TEARDOWN -----
echo ""
echo -e "${YELLOW}Tearing down...${NC}"
fusermount -u "$MOUNT_DIR" 2>/dev/null || umount "$MOUNT_DIR" 2>/dev/null
kill $FUSE_PID 2>/dev/null
wait $FUSE_PID 2>/dev/null
sleep 1
rm -rf "$TEST_DIR"

echo ""
echo "============================================"
echo -e " Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC} out of $((PASS+FAIL)) tests"
echo "============================================"

[ $FAIL -eq 0 ] && exit 0 || exit 1
