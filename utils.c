#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "utils.h"

void ab_append(AppendBuf* ab, const char* s, size_t len) {
    char* new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void ab_free(AppendBuf* ab) {
    free(ab->b);
}

void die(const char* msg) {
    write(STDOUT_FILENO, ESC_SEQ("2J"), 4);
    write(STDOUT_FILENO, ESC_SEQ("H"), 3);

    perror(msg);
    printf("\r");
    exit(1);
}
