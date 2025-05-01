#ifndef GRAMAS_BUF_H
#define GRAMAS_BUF_H

#include <stddef.h>

void buf_append_ch(char **buf, size_t *length, size_t *capacity, char c);
void buf_append(char **buf, size_t *length, size_t *capacity, size_t size_of, const char *e);
void buf_ensure_capacity(char **buf, size_t *capacity, size_t desired_capacity);

#endif // GRAMAS_BUF_H
