#ifndef GRAMAS_FSTREAM_READER_H
#define GRAMAS_FSTREAM_READER_H

#include <stddef.h>

#include "coro.h"

struct fstream_reader {
	coro_state_t state;
	FILE *stream;
	char *buf;
	size_t bufsize;
	size_t at;
	size_t bytes_in_buf;
};

void fstream_init(struct fstream_reader *f, FILE *stream, size_t bufsize);
int fstream_next(struct fstream_reader *f);
void fstream_destroy(struct fstream_reader *f);

#endif /* GRAMAS_FSTREAM_READER_H */
