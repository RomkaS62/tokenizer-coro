#include "buf.h"

#include <stdlib.h>
#include <string.h>

void buf_append_ch(char **buf, size_t *length, size_t *capacity, char c)
{
	buf_ensure_capacity(buf, capacity, *length + 1);
	(*buf)[(*length)++] = c;
}

void buf_append(char **buf, size_t *length, size_t *capacity, size_t size_of, const char *e)
{
	size_t cap_in_bytes;
	size_t needed_bytes;

	cap_in_bytes = *capacity * size_of;
	needed_bytes = *length * size_of + size_of;
	buf_ensure_capacity(buf, &cap_in_bytes, needed_bytes);
	*capacity = cap_in_bytes / size_of;
	memcpy(*buf + *length * size_of, e, size_of);
	*length += 1;
}

void buf_ensure_capacity(char **buf, size_t *capacity, size_t desired_capacity)
{
	size_t new_capacity;

	if (desired_capacity <= *capacity)
		return;

	if (!new_capacity)
		new_capacity = 1;

	for (new_capacity = *capacity; new_capacity < desired_capacity; new_capacity *= 2)
		;

	*buf = realloc(*buf, new_capacity);
	*capacity = new_capacity;
}
