#ifndef UTILS_H
#define UTILS_H

#define TTE_VERSION "0.0.1"
#define TTE_TAB_STOP 4
#define TTE_UNSAVED_QUIT_ATTEMTPS 1

#include <stddef.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ESC_SEQ(cmd) "\x1b["cmd

typedef struct {
    char* b;
    size_t len;
} AppendBuf;

void ab_append(AppendBuf*, const char*, size_t);
void ab_free(AppendBuf*);

void die(const char*);

#endif /* UTILS_H */