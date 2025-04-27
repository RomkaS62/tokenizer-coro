#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fstream_reader.h"

void fstream_init(struct fstream_reader *f, FILE *stream, size_t bufsize)
{
	memset(f, 0, sizeof(*f));
	f->stream = stream;
	f->bufsize = bufsize;
	f->buf = malloc(bufsize);
}

int fstream_next(struct fstream_reader *f)
{
	CO_BEGIN(f->state)

	for (;;) {
		f->bytes_in_buf = fread(f->buf, 1, f->bufsize, f->stream);

		if (!f->bytes_in_buf)
			break;

		for (f->at = 0; f->at < f->bytes_in_buf; f->at++)
			CO_YIELD(f->state, f->buf[f->at]);
	}

	fstream_destroy(f);
	CO_RETURN(f->state, EOF);

	CO_END
}

void fstream_destroy(struct fstream_reader *f)
{
	free(f->buf);
	f->buf = NULL;
	if (f->stream) fclose(f->stream);
	f->stream = NULL;
}
