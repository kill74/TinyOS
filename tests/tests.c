/* tests.c — Comprehensive test suite for TinyOS subsystems
 *
 * Tests cover: libc functions, filesystem, packet buffers, TCP state,
 * memory allocator, and edge cases that could break the kernel.
 * Each test prints PASS/FAIL and the suite exits with a count.
 */

#include "../libc/string.h"
#include "../libc/stdlib.h"
#include "../libc/stdio.h"
#include "../fs/fs.h"
#include "../net/packet.h"
#include "../net/tcp.h"
#include "../include/kmalloc.h"
#include "../include/vga.h"
#include "../include/timer.h"
#include <stdint.h>

/* ── Test harness ────────────────────────────────────────────────────────── */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    vga_printf("  %-30s ", #name); \
    test_##name(); \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK); \
        vga_printf("FAIL (%s:%d)\n", __FILE__, __LINE__); \
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ(a, b)     ASSERT((a) == (b))
#define ASSERT_NEQ(a, b)    ASSERT((a) != (b))
#define ASSERT_NULL(p)      ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p)  ASSERT((p) != NULL)

static void test_pass(void)
{
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_printf("PASS\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    tests_passed++;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  STRING TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(strlen_basic)
{
    ASSERT_EQ(strlen(""), 0);
    ASSERT_EQ(strlen("a"), 1);
    ASSERT_EQ(strlen("hello"), 5);
    test_pass();
}

TEST(strlen_edge)
{
    char buf[256];
    for (int i = 0; i < 255; i++) buf[i] = 'x';
    buf[255] = '\0';
    ASSERT_EQ(strlen(buf), 255);
    test_pass();
}

TEST(strcmp_basic)
{
    ASSERT_EQ(strcmp("abc", "abc"), 0);
    ASSERT(strcmp("abc", "abd") < 0);
    ASSERT(strcmp("abc", "abb") > 0);
    ASSERT(strcmp("ab", "abc") < 0);
    ASSERT(strcmp("abc", "ab") > 0);
    test_pass();
}

TEST(strncmp_basic)
{
    ASSERT_EQ(strncmp("abcde", "abczz", 3), 0);
    ASSERT(strncmp("abc", "abd", 3) < 0);
    ASSERT_EQ(strncmp("abc", "xyz", 0), 0);
    test_pass();
}

TEST(strcpy_strncpy)
{
    char buf[32];
    strcpy(buf, "hello");
    ASSERT_EQ(strcmp(buf, "hello"), 0);
    strncpy(buf, "world", 3);
    ASSERT_EQ(buf[0], 'w');
    ASSERT_EQ(buf[1], 'o');
    ASSERT_EQ(buf[2], 'r');
    ASSERT_EQ(buf[3], 'l'); /* strncpy doesn't null-terminate if n < len */
    test_pass();
}

TEST(strcat_basic)
{
    char buf[32] = "foo";
    strcat(buf, "bar");
    ASSERT_EQ(strcmp(buf, "foobar"), 0);
    test_pass();
}

TEST(strchr_strrchr)
{
    const char *s = "hello world";
    ASSERT_EQ(strchr(s, 'o'), s + 4);
    ASSERT_EQ(strrchr(s, 'o'), s + 7);
    ASSERT_NULL(strchr(s, 'z'));
    ASSERT_NOT_NULL(strchr(s, '\0'));
    test_pass();
}

TEST(strstr_basic)
{
    ASSERT_NOT_NULL(strstr("hello world", "world"));
    ASSERT_NULL(strstr("hello", "xyz"));
    ASSERT_NOT_NULL(strstr("hello", ""));
    test_pass();
}

TEST(memset_memcpy_memcmp)
{
    char buf[64];
    memset(buf, 0xAA, 64);
    ASSERT_EQ((uint8_t)buf[0], 0xAA);
    ASSERT_EQ((uint8_t)buf[63], 0xAA);

    char src[] = "test123";
    char dst[16];
    memcpy(dst, src, 8);
    ASSERT_EQ(memcmp(dst, src, 8), 0);

    ASSERT(memcmp("abc", "abd", 3) < 0);
    test_pass();
}

TEST(memmove_overlap)
{
    char buf[] = "abcdef";
    memmove(buf + 1, buf, 5);
    ASSERT_EQ(buf[0], 'a');
    ASSERT_EQ(buf[1], 'a');
    ASSERT_EQ(buf[5], 'e');
    test_pass();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  STDLIB TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(atoi_basic)
{
    ASSERT_EQ(atoi("0"), 0);
    ASSERT_EQ(atoi("42"), 42);
    ASSERT_EQ(atoi("-123"), -123);
    ASSERT_EQ(atoi("  456"), 456);
    ASSERT_EQ(atoi("+789"), 789);
    test_pass();
}

TEST(itoa_roundtrip)
{
    char buf[32];
    itoa(0, buf, 10);    ASSERT_EQ(strcmp(buf, "0"), 0);
    itoa(42, buf, 10);   ASSERT_EQ(strcmp(buf, "42"), 0);
    itoa(-99, buf, 10);  ASSERT_EQ(strcmp(buf, "-99"), 0);
    itoa(255, buf, 16);  ASSERT_EQ(strcmp(buf, "ff"), 0);
    itoa(10, buf, 2);    ASSERT_EQ(strcmp(buf, "1010"), 0);
    test_pass();
}

TEST(malloc_free_basic)
{
    void *p1 = malloc(100);
    ASSERT_NOT_NULL(p1);
    void *p2 = malloc(200);
    ASSERT_NOT_NULL(p2);
    ASSERT_NEQ((uintptr_t)p1, (uintptr_t)p2);

    memset(p1, 0x42, 100);
    ASSERT_EQ(((uint8_t *)p1)[0], 0x42);
    ASSERT_EQ(((uint8_t *)p1)[99], 0x42);

    free(p1);
    free(p2);
    free(NULL); /* should be safe */
    test_pass();
}

TEST(calloc_zeroes)
{
    int *p = (int *)calloc(10, sizeof(int));
    ASSERT_NOT_NULL(p);
    for (int i = 0; i < 10; i++)
        ASSERT_EQ(p[i], 0);
    free(p);
    test_pass();
}

TEST(abs_basic)
{
    ASSERT_EQ(abs(5), 5);
    ASSERT_EQ(abs(-5), 5);
    ASSERT_EQ(abs(0), 0);
    test_pass();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  STDIO (printf) TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(sprintf_basic)
{
    char buf[64];
    int n = sprintf(buf, "hello %s %d", "world", 42);
    ASSERT_EQ(strcmp(buf, "hello world 42"), 0);
    ASSERT_EQ(n, 15);
    test_pass();
}

TEST(sprintf_hex)
{
    char buf[64];
    sprintf(buf, "0x%x", 255);
    ASSERT_EQ(strcmp(buf, "0xff"), 0);
    sprintf(buf, "0x%X", 0xABCD);
    ASSERT_EQ(strcmp(buf, "0xABCD"), 0);
    test_pass();
}

TEST(sprintf_width_padding)
{
    char buf[64];
    sprintf(buf, "%05d", 42);
    ASSERT_EQ(strcmp(buf, "00042"), 0);
    sprintf(buf, "%8s", "hi");
    ASSERT_EQ(strcmp(buf, "      hi"), 0);
    test_pass();
}

TEST(sprintf_null_safety)
{
    char buf[64];
    sprintf(buf, "%s", NULL);
    ASSERT_EQ(strcmp(buf, "(null)"), 0);
    test_pass();
}

TEST(snprintf_truncate)
{
    char buf[8];
    int n = snprintf(buf, 8, "hello world 12345");
    ASSERT_EQ(strlen(buf), 7);
    ASSERT_EQ(buf[7], '\0');
    ASSERT(n > 7); /* returns what would have been written */
    test_pass();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  FILESYSTEM TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(fs_create_open_read)
{
    /* Create a file */
    int rc = fs_create("/testfile", FS_TYPE_FILE);
    ASSERT_EQ(rc, FS_OK);

    /* Open and write */
    int fd = fs_open("/testfile", 2);
    ASSERT(fd >= 0);
    const char *msg = "hello filesystem!";
    int written = fs_write(fd, msg, strlen(msg));
    ASSERT_EQ(written, (int)strlen(msg));
    fs_close(fd);

    /* Reopen and read */
    fd = fs_open("/testfile", 0);
    ASSERT(fd >= 0);
    char buf[64];
    int nread = fs_read(fd, buf, 64);
    ASSERT_EQ(nread, (int)strlen(msg));
    buf[nread] = '\0';
    ASSERT_EQ(strcmp(buf, msg), 0);
    fs_close(fd);

    /* Cleanup */
    fs_unlink("/testfile");
    test_pass();
}

TEST(fs_duplicate_create)
{
    fs_create("/dup", FS_TYPE_FILE);
    int rc = fs_create("/dup", FS_TYPE_FILE);
    ASSERT_EQ(rc, FS_ERR_EXIST);
    fs_unlink("/dup");
    test_pass();
}

TEST(fs_nonexistent)
{
    int fd = fs_open("/noexist", 0);
    ASSERT_EQ(fd, FS_ERR_NOENT);
    test_pass();
}

TEST(fs_seek_tell)
{
    fs_create("/seektest", FS_TYPE_FILE);
    int fd = fs_open("/seektest", 2);
    fs_write(fd, "0123456789", 10);

    int pos = fs_seek(fd, 5, FS_SEEK_SET);
    ASSERT_EQ(pos, 5);
    ASSERT_EQ(fs_tell(fd), 5);

    char c;
    fs_read(fd, &c, 1);
    ASSERT_EQ(c, '5');

    pos = fs_seek(fd, -3, FS_SEEK_CUR);
    ASSERT_EQ(pos, 3);
    fs_read(fd, &c, 1);
    ASSERT_EQ(c, '3');

    fs_close(fd);
    fs_unlink("/seektest");
    test_pass();
}

TEST(fs_truncate)
{
    fs_create("/trunc", FS_TYPE_FILE);
    int fd = fs_open("/trunc", 2);
    fs_write(fd, "long data here", 14);
    fs_close(fd);

    fs_truncate("/trunc");
    fd = fs_open("/trunc", 0);
    char buf[16];
    int n = fs_read(fd, buf, 16);
    ASSERT_EQ(n, 0);
    fs_close(fd);
    fs_unlink("/trunc");
    test_pass();
}

TEST(fs_many_files)
{
    /* Create many files to stress the allocator */
    char name[32];
    int created = 0;
    for (int i = 0; i < 50; i++) {
        sprintf(name, "/file%d", i);
        if (fs_create(name, FS_TYPE_FILE) == FS_OK)
            created++;
    }
    ASSERT(created > 0);

    /* Clean up */
    for (int i = 0; i < 50; i++) {
        sprintf(name, "/file%d", i);
        fs_unlink(name);
    }
    test_pass();
}

TEST(fs_large_write)
{
    fs_create("/bigfile", FS_TYPE_FILE);
    int fd = fs_open("/bigfile", 2);
    char buf[4096];
    memset(buf, 'X', 4096);

    /* Write 4KB (should use 1 full block) */
    int written = fs_write(fd, buf, 4096);
    ASSERT_EQ(written, 4096);
    fs_close(fd);

    /* Read it back */
    fd = fs_open("/bigfile", 0);
    char readbuf[4096];
    int n = fs_read(fd, readbuf, 4096);
    ASSERT_EQ(n, 4096);
    ASSERT_EQ(memcmp(buf, readbuf, 4096), 0);
    fs_close(fd);
    fs_unlink("/bigfile");
    test_pass();
}

TEST(fs_write_read_multiple)
{
    fs_create("/multi", FS_TYPE_FILE);
    int fd = fs_open("/multi", 2);

    fs_write(fd, "AAA", 3);
    fs_write(fd, "BBB", 3);
    fs_write(fd, "CCC", 3);
    fs_close(fd);

    fd = fs_open("/multi", 0);
    char buf[16];
    int n = fs_read(fd, buf, 16);
    ASSERT_EQ(n, 9);
    buf[9] = '\0';
    ASSERT_EQ(strcmp(buf, "AAABBBCCC"), 0);
    fs_close(fd);
    fs_unlink("/multi");
    test_pass();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  PACKET BUFFER TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(packet_alloc_free)
{
    packet_buf_t *p = packet_alloc();
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->len, 0);
    ASSERT_EQ(p->offset, 0);
    packet_free(p);
    test_pass();
}

TEST(packet_exhaust_pool)
{
    /* Allocate all 32 buffers */
    packet_buf_t *bufs[32];
    for (int i = 0; i < 32; i++) {
        bufs[i] = packet_alloc();
        ASSERT_NOT_NULL(bufs[i]);
    }
    /* Next allocation should fail */
    packet_buf_t *extra = packet_alloc();
    ASSERT_NULL(extra);

    /* Free one, should be able to alloc again */
    packet_free(bufs[0]);
    extra = packet_alloc();
    ASSERT_NOT_NULL(extra);
    packet_free(extra);

    /* Free all */
    for (int i = 1; i < 32; i++)
        packet_free(bufs[i]);
    test_pass();
}

TEST(packet_write_read)
{
    packet_buf_t *p = packet_alloc();
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    packet_write(p, data, 4);
    ASSERT_EQ(p->len, 4);
    ASSERT_EQ(memcmp(packet_data(p), data, 4), 0);

    packet_advance(p, 2);
    ASSERT_EQ(packet_remaining(p), 2);
    ASSERT_EQ(packet_data(p)[0], 0xBE);

    packet_reset(p);
    ASSERT_EQ(p->offset, 0);
    ASSERT_EQ(packet_remaining(p), 4);

    packet_free(p);
    test_pass();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  MEMORY ALLOCATOR STRESS TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(kmalloc_stress)
{
    /* Allocate and free many blocks of varying sizes */
    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        size_t sz = (i % 7 + 1) * 16;
        ptrs[i] = kmalloc(sz);
        ASSERT_NOT_NULL(ptrs[i]);
        memset(ptrs[i], i & 0xFF, sz);
    }

    /* Verify data integrity */
    for (int i = 0; i < 100; i++) {
        size_t sz = (i % 7 + 1) * 16;
        ASSERT_EQ(((uint8_t *)ptrs[i])[0], (uint8_t)(i & 0xFF));
        ASSERT_EQ(((uint8_t *)ptrs[i])[sz - 1], (uint8_t)(i & 0xFF));
    }

    /* Free in alternating order */
    for (int i = 0; i < 100; i += 2)
        kfree(ptrs[i]);
    for (int i = 1; i < 100; i += 2)
        kfree(ptrs[i]);

    test_pass();
}

TEST(kmalloc_zero_size)
{
    /* Zero-size allocation should be safe */
    void *p = kmalloc(0);
    /* Either NULL or a valid small pointer — both are acceptable */
    (void)p;
    test_pass();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  EDGE CASE / BOUNDARY TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(sprintf_overflow)
{
    char buf[8];
    /* Should not overflow — snprintf truncates */
    snprintf(buf, 8, "this is a very long string %d", 12345);
    ASSERT(strlen(buf) < 8);
    test_pass();
}

TEST(memset_then_strcmp)
{
    char buf[16];
    memset(buf, 0, 16);
    ASSERT_EQ(strlen(buf), 0);
    strcpy(buf, "test");
    ASSERT_EQ(strlen(buf), 4);
    test_pass();
}

TEST(integer_edge_cases)
{
    char buf[32];
    itoa(0, buf, 10);
    ASSERT_EQ(strcmp(buf, "0"), 0);

    itoa(2147483647, buf, 10); /* INT_MAX */
    ASSERT_EQ(strcmp(buf, "2147483647"), 0);

    itoa(-2147483647 - 1, buf, 10); /* INT_MIN */
    ASSERT_EQ(strcmp(buf, "-2147483648"), 0);

    test_pass();
}

TEST(atoi_whitespace)
{
    ASSERT_EQ(atoi("  \t  42"), 42);
    ASSERT_EQ(atoi("007"), 7);
    ASSERT_EQ(atoi("not a number"), 0);
    test_pass();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TCP CONNECTION STATE TESTS
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(tcp_alloc_free)
{
    int idx = tcp_alloc();
    ASSERT(idx >= 0);
    tcp_free(idx);
    /* Re-alloc should succeed */
    int idx2 = tcp_alloc();
    ASSERT(idx2 >= 0);
    tcp_free(idx2);
    test_pass();
}

TEST(tcp_exhaust_connections)
{
    int indices[TCP_MAX_CONN];
    for (int i = 0; i < TCP_MAX_CONN; i++) {
        indices[i] = tcp_alloc();
        ASSERT(indices[i] >= 0);
    }
    /* Should fail now */
    int extra = tcp_alloc();
    ASSERT_EQ(extra, -1);

    /* Free one — should be able to alloc again */
    tcp_free(indices[0]);
    extra = tcp_alloc();
    ASSERT(extra >= 0);
    tcp_free(extra);

    for (int i = 1; i < TCP_MAX_CONN; i++)
        tcp_free(indices[i]);
    test_pass();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  TEST RUNNER
 * ═══════════════════════════════════════════════════════════════════════════ */

void run_all_tests(void)
{
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_printf("\n=== TinyOS Test Suite ===\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* String tests */
    RUN(strlen_basic);
    RUN(strlen_edge);
    RUN(strcmp_basic);
    RUN(strncmp_basic);
    RUN(strcpy_strncpy);
    RUN(strcat_basic);
    RUN(strchr_strrchr);
    RUN(strstr_basic);
    RUN(memset_memcpy_memcmp);
    RUN(memmove_overlap);

    /* stdlib tests */
    RUN(atoi_basic);
    RUN(itoa_roundtrip);
    RUN(malloc_free_basic);
    RUN(calloc_zeroes);
    RUN(abs_basic);

    /* stdio tests */
    RUN(sprintf_basic);
    RUN(sprintf_hex);
    RUN(sprintf_width_padding);
    RUN(sprintf_null_safety);
    RUN(snprintf_truncate);

    /* Filesystem tests */
    RUN(fs_create_open_read);
    RUN(fs_duplicate_create);
    RUN(fs_nonexistent);
    RUN(fs_seek_tell);
    RUN(fs_truncate);
    RUN(fs_many_files);
    RUN(fs_large_write);
    RUN(fs_write_read_multiple);

    /* Packet buffer tests */
    RUN(packet_alloc_free);
    RUN(packet_exhaust_pool);
    RUN(packet_write_read);

    /* Memory stress tests */
    RUN(kmalloc_stress);
    RUN(kmalloc_zero_size);

    /* Edge cases */
    RUN(sprintf_overflow);
    RUN(memset_then_strcmp);
    RUN(integer_edge_cases);
    RUN(atoi_whitespace);

    /* TCP tests */
    RUN(tcp_alloc_free);
    RUN(tcp_exhaust_connections);

    /* Summary */
    vga_printf("\n--- Results ---\n");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_printf("  Passed: %d\n", tests_passed);
    if (tests_failed > 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  FAILED: %d\n", tests_failed);
    } else {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_printf("  All tests passed!\n");
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_printf("---\n\n");
}
