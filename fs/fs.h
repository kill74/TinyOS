/* fs.h — TinyFS: custom inode-based filesystem for TinyOS
 *
 * A clean, simple filesystem designed for educational use.
 * Currently runs entirely in memory (RAM-backed), with an API
 * that maps naturally to a future block-device backend.
 *
 * Layout:
 *   ┌──────────┐  Block 0
 *   │ Superblock│
 *   ├──────────┤  Block 1
 *   │ Inode Tbl│  (up to 64 inodes)
 *   ├──────────┤  Block 2
 *   │ Block Map│  (bitmap, 512 blocks)
 *   ├──────────┤  Block 3
 *   │ Data Blks│  (blocks 3–511)
 *   └──────────┘
 *
 * Features:
 *   - File create, read, write, delete, truncate
 *   - Directory listing
 *   - File metadata (size, type, timestamps)
 *   - Simple block allocator with bitmap
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* ── Filesystem constants ────────────────────────────────────────────────── */
#define FS_BLOCK_SIZE     4096
#define FS_MAX_FILES      64
#define FS_MAX_BLOCKS     512
#define FS_MAX_NAME       32
#define FS_MAX_PATH       128
#define FS_MAX_OPEN       16
#define FS_DIRECT_BLKS    8
#define FS_MAGIC          0x54494E59  /* "TINY" */

/* File types */
#define FS_TYPE_FREE      0
#define FS_TYPE_FILE      1
#define FS_TYPE_DIR       2

/* Seek origins */
#define FS_SEEK_SET       0
#define FS_SEEK_CUR       1
#define FS_SEEK_END       2

/* Error codes */
#define FS_OK             0
#define FS_ERR_NOENT     -1   /* No such file */
#define FS_ERR_EXIST     -2   /* Already exists */
#define FS_ERR_NOSPC     -3   /* No space */
#define FS_ERR_NOMEM     -4   /* Too many open files */
#define FS_ERR_BADFD     -5   /* Bad file descriptor */
#define FS_ERR_ISDIR     -6   /* Is a directory */
#define FS_ERR_NOTDIR    -7   /* Not a directory */
#define FS_ERR_INVAL     -8   /* Invalid argument */
#define FS_ERR_NFILE     -9   /* Too many files */

/* ── On-disk structures ──────────────────────────────────────────────────── */

typedef struct {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t total_inodes;
    uint32_t free_blocks;
    uint32_t inode_table_block;   /* Block number of inode table */
    uint32_t bitmap_block;        /* Block number of block bitmap */
    uint32_t data_start_block;    /* First data block */
} __attribute__((packed)) fs_superblock_t;

typedef struct {
    uint32_t size;                /* File size in bytes */
    uint8_t  type;                /* FS_TYPE_FREE, FS_TYPE_FILE, FS_TYPE_DIR */
    uint8_t  reserved[3];
    uint32_t blocks[FS_DIRECT_BLKS]; /* Direct block pointers */
    uint32_t created;             /* Creation tick */
    uint32_t modified;            /* Last modification tick */
} __attribute__((packed)) fs_inode_t;

typedef struct {
    char     name[FS_MAX_NAME];
    uint32_t inode;               /* Inode number, 0 = free entry */
} __attribute__((packed)) fs_dirent_t;

/* ── Open file descriptor ────────────────────────────────────────────────── */
typedef struct {
    uint32_t inode_no;
    uint32_t offset;
    uint8_t  mode;                /* 0=read, 1=write, 2=read+write */
    uint8_t  in_use;
} fs_fd_t;

/* ── Directory listing iterator ──────────────────────────────────────────── */
typedef struct {
    char     name[FS_MAX_NAME];
    uint32_t size;
    uint8_t  type;
} fs_stat_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Initialise the filesystem (formats if no valid superblock found) */
int fs_init(void);

/* Format the filesystem (destroys all data) */
int fs_format(void);

/* ── File operations ─────────────────────────────────────────────────────── */

/* Create a file. Returns FS_OK or error code. */
int fs_create(const char *path, uint8_t type);

/* Open a file. Returns file descriptor (>= 0) or error code (< 0). */
int fs_open(const char *path, uint8_t mode);

/* Close a file descriptor. */
int fs_close(int fd);

/* Read `count` bytes into `buf` at current offset. Returns bytes read. */
int fs_read(int fd, void *buf, uint32_t count);

/* Write `count` bytes from `buf` at current offset. Returns bytes written. */
int fs_write(int fd, const void *buf, uint32_t count);

/* Seek to a position. Returns new offset or error. */
int fs_seek(int fd, int offset, int whence);

/* Get current position. */
int fs_tell(int fd);

/* Delete a file. */
int fs_unlink(const char *path);

/* Truncate a file to zero length. */
int fs_truncate(const char *path);

/* ── Directory operations ────────────────────────────────────────────────── */

/* List files. Calls callback for each entry. */
int fs_ls(int (*callback)(const fs_stat_t *stat, void *ctx), void *ctx);

/* Get file info. Returns FS_OK or error. */
int fs_stat(const char *path, fs_stat_t *out);

/* ── Diagnostics ─────────────────────────────────────────────────────────── */

/* Print filesystem statistics */
void fs_stats(void);
