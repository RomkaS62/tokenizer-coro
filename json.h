#ifndef GRAMAS_JSON_READER_H
#define GRAMAS_JSON_READER_H

#include "coro.h"

#include <stddef.h>

enum json_token_kind_e {
	JSON_TOK_ERROR = -1,
	JSON_TOK_NONE = 0,
	JSON_TOK_STRING,
	JSON_TOK_FLOAT,
	JSON_TOK_INT,
	JSON_TOK_COLON,
	JSON_TOK_COMMA,
	JSON_TOK_NAKED_WORD,
	JSON_TOK_LEFT_CURLY_BRACE,
	JSON_TOK_RIGHT_CURLY_BRACE,
	JSON_TOK_LEFT_SQUARE_BRACE,
	JSON_TOK_RIGHT_SQUARE_BRACE,
};

struct json_tokenizer_t {
	coro_state_t state;

	void *cs;
	int (*cs_getch)(void *);

	void *error_handler;
	void (*on_error)(
			void *error_handler,
			const char *unexpected_token,
			size_t length,
			size_t linenum,
			size_t char_pos);

	char *token;
	size_t length;
	size_t capacity;
	size_t linenum;
	size_t char_pos;
	enum json_token_kind_e kind;
	int c;
};

void json_tokenizer_init(struct json_tokenizer_t *t, void *cs, int (*cs_getch)(void *));
enum json_token_kind_e json_tokenizer_next(struct json_tokenizer_t *t);
void json_tokenizer_destroy(struct json_tokenizer_t *t);

const char * json_tok_kind_to_str(enum json_token_kind_e kind);

enum json_value_type_e {
	JSON_NONE = 0,
	JSON_OBJECT = 1,
	JSON_ARRAY = 1 << 1,
	JSON_INT = 1 << 2,
	JSON_FLOAT = 1 << 3,
	JSON_STRING = 1 << 4,
	JSON_BOOL = 1 << 5,
	JSON_NULL = 1 << 6,

	JSON_NUMBER = JSON_INT | JSON_FLOAT
};

struct json_value_t;

struct json_string_t {
	char *text;
	size_t length;
};

struct json_array_t {
	size_t length;
	size_t capacity;
	struct json_value_t *values;
};

struct json_object_t {
	size_t length;
	size_t capacity;
	struct json_kv_pair_t *fields;
};

struct json_value_t {
	enum json_value_type_e type;

	union {
		struct json_object_t object;
		struct json_array_t array;
		struct json_string_t string;
		double n_float;
		int64_t n_int;
	};
};

struct json_kv_pair_t {
	struct json_string_t name;
	struct json_value_t value;
};

int json_value_parse(struct json_tokenizer_t *t, struct json_value_t *v);

void json_value_object_init(struct json_value_t *v);
void json_value_object_put(
		struct json_value_t *v,
		struct json_string_t *name,
		struct json_value_t *val);

void json_value_array_init(struct json_value_t *v);
void json_value_array_append(struct json_value_t *a, struct json_value_t *v);

void json_value_object_init(struct json_value_t *v);
void json_value_array_init(struct json_value_t *v);
void json_value_string_init(struct json_value_t *v, const char *text, size_t length);
void json_value_int_init(struct json_value_t *v, int64_t i);
void json_value_float_init(struct json_value_t *v, double d);
void json_value_bool_init(struct json_value_t *v, int b);
void json_value_null_init(struct json_value_t *v);

void json_string_set(struct json_string_t *jstr, const char *text, size_t length);
void json_string_move(struct json_string_t *from, struct json_string_t *to);
void json_string_copy(const struct json_string_t *from, struct json_string_t *to);
void json_string_destroy(struct json_string_t *jstr);
int json_string_cmp(const struct json_string_t *a, const struct json_string_t *b);

void json_value_object_put(struct json_value_t *v, struct json_string_t *name, struct json_value_t *val);

void json_value_copy(const struct json_value_t *from, struct json_value_t *to);
void json_value_move(struct json_value_t *from, struct json_value_t *to);
void json_value_destroy(struct json_value_t *v);

void json_value_to_string(
		const struct json_value_t *v,
		void *sink,
		void (*sink_write)(void *sink, const char *text, size_t length));

#endif // GRAMAS_JSON_READER_H
