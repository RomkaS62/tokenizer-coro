#include "tokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tok_init(struct tokenizer *t, void *cs, int (*cs_getch)(void *cs))
{
	memset(t, 0, sizeof(*t));
	t->char_source = cs;
	t->cs_getch = cs_getch;
}

static void tok_append_char(struct tokenizer *t, char c);
static void tok_clear(struct tokenizer *t);
static int tok_getch(struct tokenizer *t);

#define INIT_STRBUF_SIZE 32

int tok_next(struct tokenizer *t)
{
	CO_BEGIN(t->state)

	t->text = malloc(INIT_STRBUF_SIZE);
	t->capacity = INIT_STRBUF_SIZE;

	for (t->c = tok_getch(t); t->c != EOF; tok_clear(t)) {
		for (;isspace(t->c); t->c = tok_getch(t))
			;

		if (t->c == EOF) {
			tok_destroy(t);
			CO_RETURN(t->state, 0);
		}

		if (isalpha(t->c)) {
			t->kind = IDENTIFIER;

			for (; isalnum(t->c) || t->c == '_'; t->c = tok_getch(t))
				tok_append_char(t, t->c);
		} else if (isdigit(t->c)) {
			t->kind = INTEGER;

			for (; isdigit(t->c); t->c = tok_getch(t))
				tok_append_char(t, t->c);
		} else if (t->c == '"') {
			t->kind = STRING;

			for (t->c = tok_getch(t); t->c != '"' && t->c != EOF; t->c = tok_getch(t)) {
				// Newlines inside strings are verboten.
				if (t->c == '\n' || t->c == '\r')
					goto err;

				if (t->c == '\\') {
					t->c = tok_getch(t);

					// Newlines are verboten even after a backslash.
					if (t->c == EOF || t->c == '\n' || t->c '\r')
						goto err;

					if (t->c == 'n') tok_append_char(t, '\n');
					else if (t->c == 'r') tok_append_char(t, '\r');
					else if (t->c == 'f') tok_append_char(t, '\f');
					else if (t->c == 'v') tok_append_char(t, '\v');
					else if (t->c == '0') tok_append_char(t, '\0');
					else tok_append_char(t, t->c);
				} else {
					tok_append_char(t, t->c);
				}
			}

			if (t->c != EOF)
				t->c = tok_getch(t);
		} else {
			t->kind = UNKNOWN;
			tok_append_char(t, t->c);
			t->c = tok_getch(t);
		}

		tok_append_char(t, '\0');
		CO_YIELD(t->state, 1);
	}

	tok_destroy(t);
	CO_RETURN(t->state, 0);

err:
	tok_destroy(t);
	CO_RETURN(t->state, -1);

	CO_END
}

void tok_destroy(struct tokenizer *t)
{
	free(t->text);
	t->text = NULL;
	t->length = 0;
	t->capacity = 0;
	t->kind = UNKNOWN;
}

static void buf_append_ch(char **buf, size_t *length, size_t *capacity, char c);

static void tok_append_char(struct tokenizer *t, char c)
{
	buf_append_ch(&t->text, &t->length, &t->capacity, c);
}

static void buf_ensure_capacity(char **buf, size_t *capacity, size_t desired_capacity);

static void buf_append_ch(char **buf, size_t *length, size_t *capacity, char c)
{
	buf_ensure_capacity(buf, capacity, *length + 1);
	(*buf)[(*length)++] = c;
}

static void buf_ensure_capacity(char **buf, size_t *capacity, size_t desired_capacity)
{
	size_t new_capacity;

	if (desired_capacity <= *capacity)
		return;

	for (new_capacity = *capacity; new_capacity < desired_capacity; new_capacity *= 2)
		;

	*buf = realloc(*buf, new_capacity);
	*capacity = new_capacity;
}

static void tok_clear(struct tokenizer *t)
{
	t->kind = UNKNOWN;
	t->length = 0;
}

static int tok_getch(struct tokenizer *t)
{
	return t->cs_getch(t->char_source);
}

const char *tok_kind_to_string(enum token_kind kind)
{
	switch (kind) {
		case UNKNOWN: return "unkown";
		case IDENTIFIER: return "identifier";
		case INTEGER: return "integer";
		case STRING: return "string";
		default: return "<error>";
	}
}
