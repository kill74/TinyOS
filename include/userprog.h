/* userprog.h — Minimal user-program runner interface
 * Provides a tiny, in-kernel loader/interpreter for a small 'user program'
 * blob that exercises the OS via a controlled instruction set.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* External blob containing the user program instructions */
extern const uint8_t userprog_blob[];
extern const size_t userprog_blob_size;

/* Entry point that executes the loaded user program (interpreter). */
void userprog_run(void);
