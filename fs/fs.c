/* fs.c — TinyFS: custom inode-based filesystem implementation
 *
 * All storage lives in a static memory region. Block 0 is the superblock,
 * block 1 holds the inode table, block 2 holds the allocation bitmap,
 * and blocks 3..511 are data blocks.
 *
 * Each inode supports 8 direct block pointers → max 32 KB per file.
 * A root directory is inode 0; all files are children of root.
 */

#include "../fs/fs.h"
#include "../include/log.h"
#include "../include/timer.h"
#include <stdint.h>

/* ── Internal: raw block storage ─────────────────────────────────────────── */
/* Reserve a 2 MB region for the filesystem (512 × 4096-byte blocks).
 * Placed after the kernel heap region. */
#define FS_STORAGE_BASE  0x00500000   /* 5 MB mark */
static uint8_t *fs_storage = (uint8_t *)FS_STORAGE_BASE;

/* Cached pointers into the storage */
static fs_superblock_t *sb;
static fs_inode_t      *inode_table;
static uint8_t         *block_bitmap;

/* Open file table */
static fs_fd_t open_table[FS_MAX_OPEN];

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Get a pointer to a block's data */
static void *blk_ptr(uint32_t block_no)
{
    return fs_storage + (uint64_t)block_no * FS_BLOCK_SIZE;
}

/* Zero-fill a block */
static void blk_clear(uint32_t block_no)
{
    uint8_t *p = (uint8_t *)blk_ptr(block_no);
    for (int i = 0; i < FS_BLOCK_SIZE; i++) p[i] = 0;
}

/* Block allocator: find and mark a free block. Returns block number or 0. */
static uint32_t blk_alloc(void)
{
    for (uint32_t i = sb->data_start_block; i < FS_MAX_BLOCKS; i++) {
        uint32_t byte = i / 8;
        uint8_t  bit  = 1u << (i % 8);
        if (!(block_bitmap[byte] & bit)) {
            block_bitmap[byte] |= bit;
            sb->free_blocks--;
            blk_clear(i);
            return i;
        }
    }
    return 0; /* no free blocks */
}

/* Free a block */
static void blk_free(uint32_t block_no)
{
    if (block_no < sb->data_start_block || block_no >= FS_MAX_BLOCKS) return;
    uint32_t byte = block_no / 8;
    uint8_t  bit  = 1u << (block_no % 8);
    if (block_bitmap[byte] & bit) {
        block_bitmap[byte] &= ~bit;
        sb->free_blocks++;
    }
}

/* Inode helpers */
static fs_inode_t *iget(uint32_t ino)
{
    if (ino >= FS_MAX_FILES) return NULL;
    return &inode_table[ino];
}

static int inode_alloc(void)
{
    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        if (inode_table[i].type == FS_TYPE_FREE) {
            /* Clear it */
            inode_table[i].size = 0;
            inode_table[i].type = FS_TYPE_FILE;
            for (int b = 0; b < FS_DIRECT_BLKS; b++)
                inode_table[i].blocks[b] = 0;
            inode_table[i].created  = timer_get_ticks();
            inode_table[i].modified = inode_table[i].created;
            return (int)i;
        }
    }
    return -1;
}

static void inode_free(uint32_t ino)
{
    fs_inode_t *ip = iget(ino);
    if (!ip) return;
    /* Free all data blocks */
    for (int i = 0; i < FS_DIRECT_BLKS; i++) {
        if (ip->blocks[i]) {
            blk_free(ip->blocks[i]);
            ip->blocks[i] = 0;
        }
    }
    ip->type = FS_TYPE_FREE;
    ip->size = 0;
}

/* ── Path lookup ─────────────────────────────────────────────────────────── */

/* Parse "/filename" into the filename portion. Returns inode number or -1. */
static int path_lookup(const char *path)
{
    /* Skip leading slash */
    if (*path == '/') path++;
    if (*path == '\0') return 0; /* root directory */

    /* Extract filename */
    const char *name = path;
    int namelen = 0;
    while (name[namelen] && name[namelen] != '/' && namelen < FS_MAX_NAME - 1)
        namelen++;

    /* Search root directory (inode 0) */
    fs_inode_t *root = iget(0);
    if (!root || root->type != FS_TYPE_DIR) return -1;

    /* Iterate directory entries */
    uint32_t entries = root->size / sizeof(fs_dirent_t);
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t blk_idx  = (i * sizeof(fs_dirent_t)) / FS_BLOCK_SIZE;
        uint32_t blk_off  = (i * sizeof(fs_dirent_t)) % FS_BLOCK_SIZE;

        if (blk_idx >= FS_DIRECT_BLKS || !root->blocks[blk_idx]) continue;

        fs_dirent_t *de = (fs_dirent_t *)((uint8_t *)blk_ptr(root->blocks[blk_idx]) + blk_off);

        /* Compare name */
        int match = 1;
        for (int j = 0; j < namelen; j++) {
            if (de->name[j] != name[j]) { match = 0; break; }
        }
        if (match && de->name[namelen] == '\0' && de->inode != 0)
            return (int)de->inode;
    }

    return -1;
}

/* Add an entry to the root directory. Returns FS_OK or error. */
static int dir_add(uint32_t ino, const char *name)
{
    fs_inode_t *root = iget(0);
    if (!root) return FS_ERR_INVAL;

    uint32_t entries = root->size / sizeof(fs_dirent_t);

    /* Check if name already exists */
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t blk_idx  = (i * sizeof(fs_dirent_t)) / FS_BLOCK_SIZE;
        uint32_t blk_off  = (i * sizeof(fs_dirent_t)) % FS_BLOCK_SIZE;
        if (blk_idx >= FS_DIRECT_BLKS || !root->blocks[blk_idx]) continue;

        fs_dirent_t *de = (fs_dirent_t *)((uint8_t *)blk_ptr(root->blocks[blk_idx]) + blk_off);
        if (de->inode != 0) {
            int same = 1;
            for (int j = 0; j < FS_MAX_NAME; j++) {
                if (de->name[j] != name[j]) { same = 0; break; }
                if (de->name[j] == '\0') break;
            }
            if (same) return FS_ERR_EXIST;
        }
    }

    /* Find a free directory entry */
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t blk_idx  = (i * sizeof(fs_dirent_t)) / FS_BLOCK_SIZE;
        uint32_t blk_off  = (i * sizeof(fs_dirent_t)) % FS_BLOCK_SIZE;
        if (blk_idx >= FS_DIRECT_BLKS || !root->blocks[blk_idx]) continue;

        fs_dirent_t *de = (fs_dirent_t *)((uint8_t *)blk_ptr(root->blocks[blk_idx]) + blk_off);
        if (de->inode == 0) {
            /* Reuse free entry */
            for (int j = 0; j < FS_MAX_NAME; j++) de->name[j] = name[j];
            de->inode = ino;
            return FS_OK;
        }
    }

    /* Need a new entry — allocate a block if needed */
    uint32_t new_idx = entries;
    uint32_t blk_idx  = (new_idx * sizeof(fs_dirent_t)) / FS_BLOCK_SIZE;
    uint32_t blk_off  = (new_idx * sizeof(fs_dirent_t)) % FS_BLOCK_SIZE;

    if (blk_idx >= FS_DIRECT_BLKS) return FS_ERR_NFILE;

    if (!root->blocks[blk_idx]) {
        root->blocks[blk_idx] = blk_alloc();
        if (!root->blocks[blk_idx]) return FS_ERR_NOSPC;
    }

    fs_dirent_t *de = (fs_dirent_t *)((uint8_t *)blk_ptr(root->blocks[blk_idx]) + blk_off);
    for (int j = 0; j < FS_MAX_NAME; j++) de->name[j] = name[j];
    de->inode = ino;
    root->size = (new_idx + 1) * sizeof(fs_dirent_t);
    root->modified = timer_get_ticks();

    return FS_OK;
}

/* Remove an entry from the root directory */
static int dir_remove(const char *name)
{
    fs_inode_t *root = iget(0);
    if (!root) return FS_ERR_INVAL;

    uint32_t entries = root->size / sizeof(fs_dirent_t);
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t blk_idx  = (i * sizeof(fs_dirent_t)) / FS_BLOCK_SIZE;
        uint32_t blk_off  = (i * sizeof(fs_dirent_t)) % FS_BLOCK_SIZE;
        if (blk_idx >= FS_DIRECT_BLKS || !root->blocks[blk_idx]) continue;

        fs_dirent_t *de = (fs_dirent_t *)((uint8_t *)blk_ptr(root->blocks[blk_idx]) + blk_off);
        if (de->inode == 0) continue;

        int match = 1;
        for (int j = 0; j < FS_MAX_NAME; j++) {
            if (de->name[j] != name[j]) { match = 0; break; }
            if (de->name[j] == '\0') break;
        }
        if (match) {
            de->inode = 0;
            root->modified = timer_get_ticks();
            return FS_OK;
        }
    }
    return FS_ERR_NOENT;
}

/* ── Ensure a block is allocated for an inode at logical index `idx` ─────── */
static int inode_ensure_block(fs_inode_t *ip, uint32_t idx)
{
    if (idx >= FS_DIRECT_BLKS) return FS_ERR_NOSPC;
    if (!ip->blocks[idx]) {
        ip->blocks[idx] = blk_alloc();
        if (!ip->blocks[idx]) return FS_ERR_NOSPC;
    }
    return FS_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int fs_init(void)
{
    sb = (fs_superblock_t *)fs_storage;

    /* Check for existing filesystem */
    if (sb->magic == FS_MAGIC &&
        sb->block_size == FS_BLOCK_SIZE &&
        sb->total_blocks == FS_MAX_BLOCKS) {
        /* Valid filesystem found */
        inode_table   = (fs_inode_t *)blk_ptr(sb->inode_table_block);
        block_bitmap  = (uint8_t *)blk_ptr(sb->bitmap_block);
        LOG_INFO("FS: existing TinyFS mounted (%u free blocks)", sb->free_blocks);
    } else {
        /* No valid FS — format */
        LOG_INFO("FS: no valid filesystem found, formatting...");
        fs_format();
    }

    /* Clear open file table */
    for (int i = 0; i < FS_MAX_OPEN; i++)
        open_table[i].in_use = 0;

    return FS_OK;
}

int fs_format(void)
{
    /* Zero everything */
    for (uint32_t i = 0; i < FS_MAX_BLOCKS * FS_BLOCK_SIZE; i++)
        fs_storage[i] = 0;

    /* Set up superblock */
    sb = (fs_superblock_t *)fs_storage;
    sb->magic           = FS_MAGIC;
    sb->block_size      = FS_BLOCK_SIZE;
    sb->total_blocks    = FS_MAX_BLOCKS;
    sb->total_inodes    = FS_MAX_FILES;
    sb->free_blocks     = FS_MAX_BLOCKS - 3; /* super + inodes + bitmap */
    sb->inode_table_block = 1;
    sb->bitmap_block      = 2;
    sb->data_start_block  = 3;

    /* Inode table */
    inode_table = (fs_inode_t *)blk_ptr(1);
    for (int i = 0; i < FS_MAX_FILES; i++) {
        inode_table[i].type = FS_TYPE_FREE;
        inode_table[i].size = 0;
    }

    /* Block bitmap */
    block_bitmap = (uint8_t *)blk_ptr(2);

    /* Mark superblock, inode table, and bitmap as used */
    block_bitmap[0] = 0x07; /* blocks 0, 1, 2 */

    /* Create root directory (inode 0) */
    inode_table[0].type     = FS_TYPE_DIR;
    inode_table[0].size     = 0;
    inode_table[0].created  = timer_get_ticks();
    inode_table[0].modified = inode_table[0].created;
    for (int b = 0; b < FS_DIRECT_BLKS; b++)
        inode_table[0].blocks[b] = 0;

    LOG_INFO("FS: formatted (%u blocks, %u inodes)", FS_MAX_BLOCKS, FS_MAX_FILES);
    return FS_OK;
}

/* ── File operations ─────────────────────────────────────────────────────── */

int fs_create(const char *path, uint8_t type)
{
    if (!path || *path == '\0') return FS_ERR_INVAL;

    /* Skip leading slash */
    const char *name = path;
    if (*name == '/') name++;
    if (*name == '\0') return FS_ERR_INVAL;

    /* Check it doesn't already exist */
    if (path_lookup(path) >= 0) return FS_ERR_EXIST;

    /* Allocate an inode */
    int ino = inode_alloc();
    if (ino < 0) return FS_ERR_NFILE;

    fs_inode_t *ip = iget((uint32_t)ino);
    ip->type = type;

    /* Add to root directory */
    int rc = dir_add((uint32_t)ino, name);
    if (rc != FS_OK) {
        inode_free((uint32_t)ino);
        return rc;
    }

    return FS_OK;
}

int fs_open(const char *path, uint8_t mode)
{
    int ino = path_lookup(path);
    if (ino < 0) return FS_ERR_NOENT;

    fs_inode_t *ip = iget((uint32_t)ino);
    if (ip->type == FS_TYPE_DIR) return FS_ERR_ISDIR;

    /* Find free fd */
    for (int i = 0; i < FS_MAX_OPEN; i++) {
        if (!open_table[i].in_use) {
            open_table[i].in_use    = 1;
            open_table[i].inode_no  = (uint32_t)ino;
            open_table[i].offset    = 0;
            open_table[i].mode      = mode;
            return i;
        }
    }
    return FS_ERR_NOMEM;
}

int fs_close(int fd)
{
    if (fd < 0 || fd >= FS_MAX_OPEN || !open_table[fd].in_use)
        return FS_ERR_BADFD;
    open_table[fd].in_use = 0;
    return FS_OK;
}

int fs_read(int fd, void *buf, uint32_t count)
{
    if (fd < 0 || fd >= FS_MAX_OPEN || !open_table[fd].in_use)
        return FS_ERR_BADFD;

    fs_fd_t *f = &open_table[fd];
    fs_inode_t *ip = iget(f->inode_no);

    if (f->offset >= ip->size) return 0;

    uint32_t to_read = count;
    if (f->offset + to_read > ip->size)
        to_read = ip->size - f->offset;

    uint8_t *dst = (uint8_t *)buf;
    uint32_t done = 0;

    while (done < to_read) {
        uint32_t blk_idx  = (f->offset + done) / FS_BLOCK_SIZE;
        uint32_t blk_off  = (f->offset + done) % FS_BLOCK_SIZE;

        if (blk_idx >= FS_DIRECT_BLKS || !ip->blocks[blk_idx])
            break;

        uint32_t chunk = FS_BLOCK_SIZE - blk_off;
        if (chunk > to_read - done) chunk = to_read - done;

        uint8_t *src = (uint8_t *)blk_ptr(ip->blocks[blk_idx]) + blk_off;
        for (uint32_t i = 0; i < chunk; i++)
            dst[done + i] = src[i];

        done += chunk;
    }

    f->offset += done;
    return (int)done;
}

int fs_write(int fd, const void *buf, uint32_t count)
{
    if (fd < 0 || fd >= FS_MAX_OPEN || !open_table[fd].in_use)
        return FS_ERR_BADFD;
    if (count == 0) return 0;

    fs_fd_t *f = &open_table[fd];
    fs_inode_t *ip = iget(f->inode_no);
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t done = 0;

    while (done < count) {
        uint32_t blk_idx  = (f->offset + done) / FS_BLOCK_SIZE;
        uint32_t blk_off  = (f->offset + done) % FS_BLOCK_SIZE;

        if (blk_idx >= FS_DIRECT_BLKS)
            break;

        /* Allocate block if needed */
        if (inode_ensure_block(ip, blk_idx) != FS_OK)
            break;

        uint32_t chunk = FS_BLOCK_SIZE - blk_off;
        if (chunk > count - done) chunk = count - done;

        uint8_t *dst = (uint8_t *)blk_ptr(ip->blocks[blk_idx]) + blk_off;
        for (uint32_t i = 0; i < chunk; i++)
            dst[i] = src[done + i];

        done += chunk;
    }

    f->offset += done;
    if (f->offset > ip->size)
        ip->size = f->offset;
    ip->modified = timer_get_ticks();

    return (int)done;
}

int fs_seek(int fd, int offset, int whence)
{
    if (fd < 0 || fd >= FS_MAX_OPEN || !open_table[fd].in_use)
        return FS_ERR_BADFD;

    fs_fd_t *f = &open_table[fd];
    fs_inode_t *ip = iget(f->inode_no);
    int new_off;

    switch (whence) {
        case FS_SEEK_SET: new_off = offset; break;
        case FS_SEEK_CUR: new_off = (int)f->offset + offset; break;
        case FS_SEEK_END: new_off = (int)ip->size + offset; break;
        default: return FS_ERR_INVAL;
    }

    if (new_off < 0) new_off = 0;
    f->offset = (uint32_t)new_off;
    return new_off;
}

int fs_tell(int fd)
{
    if (fd < 0 || fd >= FS_MAX_OPEN || !open_table[fd].in_use)
        return FS_ERR_BADFD;
    return (int)open_table[fd].offset;
}

int fs_unlink(const char *path)
{
    int ino = path_lookup(path);
    if (ino < 0) return FS_ERR_NOENT;

    /* Skip leading slash */
    const char *name = path;
    if (*name == '/') name++;

    /* Remove from directory */
    dir_remove(name);

    /* Free inode and blocks */
    inode_free((uint32_t)ino);
    return FS_OK;
}

int fs_truncate(const char *path)
{
    int ino = path_lookup(path);
    if (ino < 0) return FS_ERR_NOENT;

    fs_inode_t *ip = iget((uint32_t)ino);
    if (ip->type == FS_TYPE_DIR) return FS_ERR_ISDIR;

    /* Free all data blocks */
    for (int i = 0; i < FS_DIRECT_BLKS; i++) {
        if (ip->blocks[i]) {
            blk_free(ip->blocks[i]);
            ip->blocks[i] = 0;
        }
    }
    ip->size = 0;
    ip->modified = timer_get_ticks();
    return FS_OK;
}

/* ── Directory operations ────────────────────────────────────────────────── */

int fs_ls(int (*callback)(const fs_stat_t *stat, void *ctx), void *ctx)
{
    fs_inode_t *root = iget(0);
    if (!root || root->type != FS_TYPE_DIR) return FS_ERR_NOTDIR;

    uint32_t entries = root->size / sizeof(fs_dirent_t);
    int count = 0;

    for (uint32_t i = 0; i < entries; i++) {
        uint32_t blk_idx  = (i * sizeof(fs_dirent_t)) / FS_BLOCK_SIZE;
        uint32_t blk_off  = (i * sizeof(fs_dirent_t)) % FS_BLOCK_SIZE;
        if (blk_idx >= FS_DIRECT_BLKS || !root->blocks[blk_idx]) continue;

        fs_dirent_t *de = (fs_dirent_t *)((uint8_t *)blk_ptr(root->blocks[blk_idx]) + blk_off);
        if (de->inode == 0) continue;

        fs_inode_t *ip = iget(de->inode);
        if (!ip || ip->type == FS_TYPE_FREE) continue;

        fs_stat_t st;
        for (int j = 0; j < FS_MAX_NAME; j++) st.name[j] = de->name[j];
        st.size = ip->size;
        st.type = ip->type;

        if (callback) {
            if (callback(&st, ctx) != 0) break;
        }
        count++;
    }

    return count;
}

int fs_stat(const char *path, fs_stat_t *out)
{
    if (!out) return FS_ERR_INVAL;

    int ino = path_lookup(path);
    if (ino < 0) return FS_ERR_NOENT;

    fs_inode_t *ip = iget((uint32_t)ino);

    /* Get name from path */
    const char *name = path;
    if (*name == '/') name++;
    int j;
    for (j = 0; j < FS_MAX_NAME - 1 && name[j]; j++)
        out->name[j] = name[j];
    out->name[j] = '\0';

    out->size = ip->size;
    out->type = ip->type;
    return FS_OK;
}

/* ── Diagnostics ─────────────────────────────────────────────────────────── */

void fs_stats(void)
{
    uint32_t used_inodes = 0;
    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        if (inode_table[i].type != FS_TYPE_FREE)
            used_inodes++;
    }

    vga_printf("  TinyFS: %u/%u blocks free, %u/%u inodes used\n",
               sb->free_blocks, FS_MAX_BLOCKS,
               used_inodes, FS_MAX_FILES);
}
