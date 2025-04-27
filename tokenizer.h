#ifndef GRAMAS_TOKENIZER_H
#define GRAMAS_TOKENIZER_H

#include <stddef.h>

#include "coro.h"

enum token_kind {
	UNKNOWN,
	IDENTIFIER,
	INTEGER,
	STRING
};

struct tokenizer {
	coro_state_t state;
	char *text;
	size_t length;
	size_t capacity;
	enum token_kind kind;
	void *char_source;			// Possibly a struct fstream_reader *
	int (*cs_getch)(void *cs);	// Possibly &fstream_next
	int c;
};

void tok_init(struct tokenizer *t, void *cs, int (*cs_getch)(void *cs));
int tok_next(struct tokenizer *t);
void tok_destroy(struct tokenizer *t);

const char *tok_kind_to_string(enum token_kind kind);

#endif /* GRAMAS_TOKENIZER_H */
