/* Minimal user program blob for in-kernel execution.
 * A tiny interpreter-style blob that exercises the OS by printing and yielding.
 * Opcodes:
 *  0x01 - PRINT_STRING: next 2 bytes = length (little-endian), followed by that many chars
 *  0x02 - SLEEP: next 2 bytes = ticks (little-endian)
 *  0x03 - YIELD: yield the CPU to the scheduler
 *  0xFF - END
 */
#include <stdint.h>
#include <stddef.h>

/* Minimal in-kernel user program blob: enhanced with few opcodes */
const uint8_t userprog_blob[] = {
    0x01, 0x10, /* PRINT_STRING, length=16 */
    'H','e','l','l','o',' ','u','s','e','r','p','r','o','g','.', '\n',
    0x04, 0x39, 0x30, 0x00, 0x00, /* PRINT_INT 12345 */
    0x02, 0x05, 0x00,             /* SLEEP 5 ticks */
    0x03,                          /* YIELD */
    0x05, 0x39, 0x30, 0x00, 0x00, /* PRINT_HEX 0x00003039 (12345 in hex) */
    0x01, 0x05, 'D','o','n','e','\n', /* PRINT_STRING "Done\n" */
    0xFF                            /* END */
};

const size_t userprog_blob_size = sizeof(userprog_blob);
