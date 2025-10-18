#ifndef BOOTLOG_H
#define BOOTLOG_H

#include <stdint.h>
#include <stdbool.h>

// Initialize bootlog capture
void bootlog_init(void);

// Get the captured bootlog
// Returns a null-terminated string, or NULL if no log captured
const char* bootlog_get(void);

// Finalize bootlog capture and store the log
void bootlog_finalize(void);

#endif // BOOTLOG_H