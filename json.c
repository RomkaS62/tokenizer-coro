#include "json.h"

#include "buf.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int jt_getch(struct json_tokenizer_t *t)
{
	int ret;

	ret = t->cs_getch(t->cs);

	if (ret == '\n') {
		t->linenum++;
		t->char_pos = 0;
	} else if (ret != EOF) {
		t->char_pos++;
	}

	return ret;
}

static inline void jt_report_error(struct json_tokenizer_t *t)
{
	if (t->on_error) {
		t->on_error(t->error_handler, t->token, t->length, t->linenum, t->char_pos);
		t->on_error = NULL;
	}
}

static inline void jt_tok_append(struct json_tokenizer_t *t, char c)
{
	buf_append_ch(&t->token, &t->length, &t->capacity, c);
}

static inline int jt_consume_token(struct json_tokenizer_t *t, enum json_token_kind_e kind)
{
	if (t->kind != kind)
		return 0;

	json_tokenizer_next(t);

	return 1;
}

static inline int32_t jt_scan_code_unit(struct json_tokenizer_t *t)
{
	int i;
	char utf_strbuf[5];

	memset(utf_strbuf, 0, sizeof(utf_strbuf));

	for (i = 0; i < 4; i++, t->c = jt_getch(t)) {
		if (!isxdigit(t->c))
			return -1;

		utf_strbuf[i] = t->c;
	}

	return strtol(utf_strbuf, NULL, 16);
}

static int utf8_write_c(int32_t c, char *str)
{
	static const int32_t SIX_BITS = ~(~0 << 6);
	static const int32_t CONTINUATION_INDICATOR = (1 << 7);
	static const int32_t TWO_BYTE_INDICATOR = (1 << 7) | (1 << 6);
	static const int32_t THREE_BYTE_INDICATOR = TWO_BYTE_INDICATOR | (1 << 5);
	static const int32_t FOUR_BYTE_INDICATOR = THREE_BYTE_INDICATOR | (1 << 4);

	if (c < 0)
		return -1;

	if (c < (1 << 7)) {
		str[0] = (char)c;
		return 1;
	} else if (c < (1 << 11)) {
		str[1] = CONTINUATION_INDICATOR | (c & SIX_BITS);
		c >>= 6;
		str[0] = TWO_BYTE_INDICATOR | c;
		return 2;
	} else if (c < (1 << 16)) {
		str[2] = CONTINUATION_INDICATOR | (c & SIX_BITS);
		c >>= 6;
		str[1] = CONTINUATION_INDICATOR | (c & SIX_BITS);
		c >>= 6;
		str[0] = THREE_BYTE_INDICATOR | c;
		return 3;
	} else if (c < (1 << 21)) {
		str[3] = CONTINUATION_INDICATOR | (c & SIX_BITS);
		c >>= 6;
		str[2] = CONTINUATION_INDICATOR | (c & SIX_BITS);
		c >>= 6;
		str[1] = CONTINUATION_INDICATOR | (c & SIX_BITS);
		c >>= 6;
		str[0] = FOUR_BYTE_INDICATOR | c;
		return 4;
	}

	return -1;
}

static inline int jt_scan_string_char(struct json_tokenizer_t *t)
{
	static const int32_t TEN_BITS = ~(~0 << 10);

	int32_t high_code_unit;
	int32_t low_code_unit;
	int32_t codepoint;
	char utf8buf[4];
	int chars_written;
	int i;

	for (t->c = jt_getch(t); t->c != '"'; t->c = jt_getch(t)) {
		if (t->c == '\n' || t->c == '\r' || t->c == EOF)
			return 1;

		if (t->c == '\\') {
			t->c = jt_getch(t);

			if (t->c == '\n' || t->c == '\r' || t->c == EOF)
				return 1;

			if (t->c == 'n') { jt_tok_append(t, '\n'); }
			else if (t->c == 'r') { jt_tok_append(t, '\r'); }
			else if (t->c == 't') { jt_tok_append(t, '\t'); }
			else if (t->c == 'f') { jt_tok_append(t, '\f'); }
			else if (t->c == 'b') { jt_tok_append(t, '\b'); }
			else if (t->c == '0') { jt_tok_append(t, '\0'); }
			else if (t->c == 'u') {
				t->c = jt_getch(t);

				if ((high_code_unit = jt_scan_code_unit(t)) < 0)
					return 1;

				if ((high_code_unit & ~TEN_BITS) == 0xD800) {
					if (t->c != '\\') return -1;
					if ((t->c = jt_getch(t)) != 'u') return -1;
					t->c = jt_getch(t);

					if ((low_code_unit = jt_scan_code_unit(t)) < 0)
						return 1;

					if ((low_code_unit & ~TEN_BITS) != 0xDC00)
						return 1;

					codepoint = (high_code_unit & TEN_BITS) | ((low_code_unit & TEN_BITS) << 10);
					chars_written = utf8_write_c(codepoint, utf8buf);
				} else {
					chars_written = utf8_write_c(high_code_unit, utf8buf);
				}

				if (chars_written < 0)
					return 1;

				for (i = 0; i < chars_written; i++)
					jt_tok_append(t, utf8buf[i]);
			} else {
				jt_tok_append(t, t->c);
			};
		} else {
			jt_tok_append(t, t->c);
		}
	}

	if (t->c == EOF)
		return -1;

	if (t->c == '"')
		t->c = jt_getch(t);

	return 0;
}

static inline int jt_scan_integer(struct json_tokenizer_t *t)
{
	if (!isdigit(t->c))
		return 1;

	for (; isdigit(t->c); t->c = jt_getch(t))
		jt_tok_append(t, t->c);

	return 0;
}

static enum json_token_kind_e jt_scan_number(struct json_tokenizer_t *t)
{
	enum json_token_kind_e ret = JSON_TOK_INT;

	if (t->c == '-') {
		jt_tok_append(t, t->c);
		t->c = jt_getch(t);
	}

	if (jt_scan_integer(t))
		return JSON_TOK_ERROR;

	if (t->c == '.') {
		ret = JSON_TOK_FLOAT;
		jt_tok_append(t, t->c);
		t->c = jt_getch(t);

		if (jt_scan_integer(t))
			return JSON_TOK_ERROR;
	}

	if (t->c == 'e') {
		ret = JSON_TOK_FLOAT;
		jt_tok_append(t, t->c);
		t->c = jt_getch(t);

		if (jt_scan_integer(t))
			return JSON_TOK_ERROR;
	}

	return ret;
}

void json_tokenizer_init(struct json_tokenizer_t *t, void *cs, int (*cs_getch)(void *))
{
	memset(t, 0, sizeof(*t));
	t->cs = cs;
	t->cs_getch = cs_getch;
}

enum json_token_kind_e json_tokenizer_next(struct json_tokenizer_t *t)
{
	static const size_t INIT_CAPACITY = 32;

	CO_BEGIN(t->state)

	t->capacity = INIT_CAPACITY;
	t->token = malloc(INIT_CAPACITY);
	t->kind = JSON_TOK_NONE;

	for (t->c = jt_getch(t); t->c != EOF;) {
		t->length = 0;

		for (; t->c != EOF && isspace(t->c); t->c = jt_getch(t))
			;

		if (t->c == EOF)
			CO_RETURN(t->state, t->kind = JSON_TOK_NONE);

		if (t->c == '{') {
			jt_tok_append(t, t->c);
			t->c = jt_getch(t);
			t->kind = JSON_TOK_LEFT_CURLY_BRACE;
		} else if (t->c == '}') {
			jt_tok_append(t, t->c);
			t->c = jt_getch(t);
			t->kind = JSON_TOK_RIGHT_CURLY_BRACE;
		} else if (t->c == '[') {
			jt_tok_append(t, t->c);
			t->c = jt_getch(t);
			t->kind = JSON_TOK_LEFT_SQUARE_BRACE;
		} else if (t->c == ']') {
			jt_tok_append(t, t->c);
			t->c = jt_getch(t);
			t->kind = JSON_TOK_RIGHT_SQUARE_BRACE;
		} else if (t->c == ':') {
			jt_tok_append(t, t->c);
			t->c = jt_getch(t);
			t->kind = JSON_TOK_COLON;
		} else if (t->c == ',') {
			jt_tok_append(t, t->c);
			t->c = jt_getch(t);
			t->kind = JSON_TOK_COMMA;
		} else if (isalpha(t->c)) {
			for (; isalnum(t->c) || t->c == '_'; t->c = jt_getch(t))
				buf_append_ch(&t->token, &t->length, &t->capacity, t->c);

			t->kind = JSON_TOK_NAKED_WORD;
		} else if (isdigit(t->c) || t->c == '-') {
			t->kind = jt_scan_number(t);
		} else if (t->c == '"') {
			if (jt_scan_string_char(t))
				CO_RETURN(t->state, t->kind = JSON_TOK_ERROR);

			t->kind = JSON_TOK_STRING;
		} else {
			CO_RETURN(t->state, t->kind = JSON_TOK_ERROR);
		}

		jt_tok_append(t, '\0');
		CO_YIELD(t->state, t->kind);
	}

	CO_RETURN(t->state, t->kind = JSON_TOK_NONE);

	CO_END
}

void json_tokenizer_destroy(struct json_tokenizer_t *t)
{
	free(t->token);
	memset(t, 0, sizeof(*t));
}

const char * json_tok_kind_to_str(enum json_token_kind_e kind)
{
	switch (kind) {
		case JSON_TOK_ERROR: return "error";
		case JSON_TOK_NONE: return "none";
		case JSON_TOK_STRING: return "string";
		case JSON_TOK_FLOAT: return "float";
		case JSON_TOK_INT: return "int";
		case JSON_TOK_COLON: return "colon";
		case JSON_TOK_COMMA: return "comma";
		case JSON_TOK_NAKED_WORD: return "naked_word";
		case JSON_TOK_LEFT_CURLY_BRACE: return "left_curly_brace";
		case JSON_TOK_RIGHT_CURLY_BRACE: return "right_curly_brace";
		case JSON_TOK_LEFT_SQUARE_BRACE: return "left_square_brace";
		case JSON_TOK_RIGHT_SQUARE_BRACE: return "right_square_brace";
		default: return "undefined";
	}
}

static int json_parse_object(struct json_tokenizer_t *t, struct json_value_t *ret);
static int json_parse_kv_pair(
		struct json_tokenizer_t *t,
		struct json_string_t *k,
		struct json_value_t *v);

static int json_parse_array(struct json_tokenizer_t *t, struct json_value_t *ret);

int json_value_parse(struct json_tokenizer_t *t, struct json_value_t *ret)
{
	switch (t->kind) {
		case JSON_TOK_LEFT_CURLY_BRACE:
			return json_parse_object(t, ret);
		case JSON_TOK_LEFT_SQUARE_BRACE:
			return json_parse_array(t, ret);
		case JSON_TOK_STRING:
			json_value_string_init(ret, t->token, t->length);
			json_tokenizer_next(t);
			break;
		case JSON_TOK_INT:
			json_value_int_init(ret, strtol(t->token, NULL, 10));
			json_tokenizer_next(t);
			break;
		case JSON_TOK_FLOAT:
			json_value_float_init(ret, strtod(t->token, NULL));
			json_tokenizer_next(t);
			break;
		case JSON_TOK_NAKED_WORD:
			if (strcmp(t->token, "false") == 0) {
				json_value_bool_init(ret, 0);
			} else if (strcmp(t->token, "true") == 0) {
				json_value_bool_init(ret, 1);
			} else if (strcmp(t->token, "null") == 0) {
				json_value_null_init(ret);
			} else {
				jt_report_error(t);
				return 1;
			}

			json_tokenizer_next(t);

			break;
		default:
			jt_report_error(t);
			return 1;
	}

	return 0;
}

static int json_parse_object(struct json_tokenizer_t *t, struct json_value_t *ret)
{
	struct json_string_t str = { 0 };
	struct json_value_t val = { 0 };
	int error = 0;

	if (!jt_consume_token(t, JSON_TOK_LEFT_CURLY_BRACE))
		goto err;

	json_value_object_init(ret);

	if (jt_consume_token(t, JSON_TOK_RIGHT_CURLY_BRACE))
		goto end;

	do {
		if (json_parse_kv_pair(t, &str, &val))
			goto err;

		json_value_object_put(ret, &str, &val);
	} while (jt_consume_token(t, JSON_TOK_COMMA));

	if (!jt_consume_token(t, JSON_TOK_RIGHT_CURLY_BRACE))
		goto err;

	goto end;

err:
	jt_report_error(t);
	error = 1;

end:
	json_string_destroy(&str);
	json_value_destroy(&val);

	return error;
}

static int json_parse_kv_pair(
		struct json_tokenizer_t *t,
		struct json_string_t *k,
		struct json_value_t *v)
{
	if (t->kind != JSON_TOK_STRING) {
		jt_report_error(t);
		return 1;
	}

	json_string_set(k, t->token, t->length);
	json_tokenizer_next(t);

	if (!jt_consume_token(t, JSON_TOK_COLON)) {
		jt_report_error(t);
		return 1;
	}

	if (json_value_parse(t, v)) {
		jt_report_error(t);
		return 1;
	}

	return 0;
}

static int json_parse_array(struct json_tokenizer_t *t, struct json_value_t *ret)
{
	struct json_value_t val = { 0 };
	int error = 0;

	if (!jt_consume_token(t, JSON_TOK_LEFT_SQUARE_BRACE))
		goto err;

	json_value_array_init(ret);

	if (jt_consume_token(t, JSON_TOK_RIGHT_SQUARE_BRACE))
		goto end;

	do {
		if (json_value_parse(t, &val))
			goto err;

		json_value_array_append(ret, &val);
	} while (jt_consume_token(t, JSON_TOK_COMMA));

	if (!jt_consume_token(t, JSON_TOK_RIGHT_SQUARE_BRACE))
		goto err;

	goto end;

err:
	jt_report_error(t);
	error = 1;

end:
	json_value_destroy(&val);

	return error;
}

void json_value_array_append(struct json_value_t *a, struct json_value_t *v)
{
	buf_append((char **)&a->array.values, &a->array.length, &a->array.capacity,
			sizeof(*v), (char *)v);
	memset(v, 0, sizeof(*v));
}

void json_value_object_init(struct json_value_t *v)
{
	v->type = JSON_OBJECT;
	v->object.length = 0;
	v->object.capacity = 4;
	v->object.fields = calloc(v->object.capacity, sizeof(v->object.fields[0]));
}

void json_value_array_init(struct json_value_t *v)
{
	v->type = JSON_ARRAY;
	v->array.length = 0;
	v->array.capacity = 4;
	v->array.values = calloc(v->array.capacity, sizeof(v->array.values[0]));
}

void json_value_string_init(struct json_value_t *v, const char *token, size_t length)
{
	json_value_destroy(v);
	v->type = JSON_STRING;
	json_string_set(&v->string, token, length);
}

void json_string_set(struct json_string_t *jstr, const char *text, size_t length)
{
	jstr->text = realloc(jstr->text, length);
	jstr->length = length;
	memcpy(jstr->text, text, length);
}

void json_string_move(struct json_string_t *from, struct json_string_t *to)
{
	memcpy(to, from, sizeof(*from));
	memset(from, 0, sizeof(*from));
}

void json_string_copy(const struct json_string_t *from, struct json_string_t *to)
{
	to->length = from->length;
	to->text = malloc(to->length);
	memcpy(to->text, from->text, to->length);
}

void json_string_destroy(struct json_string_t *jstr)
{
	free(jstr->text);
	memset(jstr, 0, sizeof(*jstr));
}

int json_string_cmp(const struct json_string_t *a, const struct json_string_t *b)
{
	if (a->length > b->length)
		return 1;

	if (a->length < b->length)
		return -1;

	return memcmp(a->text, b->text, a->length);
}

void json_value_int_init(struct json_value_t *v, int64_t i)
{
	json_value_destroy(v);
	v->type = JSON_INT;
	v->n_int = i;
}

void json_value_float_init(struct json_value_t *v, double d)
{
	json_value_destroy(v);
	v->type = JSON_FLOAT;
	v->n_float = d;
}

void json_value_bool_init(struct json_value_t *v, int b)
{
	json_value_destroy(v);
	v->type = JSON_BOOL;
	v->n_int = b;
}

void json_value_null_init(struct json_value_t *v)
{
	json_value_destroy(v);
	v->type = JSON_NULL;
}

static int json_kv_pair_cmp(const struct json_kv_pair_t *a, const struct json_kv_pair_t *b);

void json_value_object_put(struct json_value_t *v, struct json_string_t *name, struct json_value_t *val)
{
	struct json_kv_pair_t kv;

	json_string_move(name, &kv.name);
	kv.value = calloc(1, sizeof(*kv.value));
	json_value_move(val, kv.value);

	buf_append((char **)&v->object.fields, &v->object.length, &v->object.capacity,
			sizeof(kv), (const char *)&kv);

	qsort(v->object.fields, v->object.length, sizeof(kv),
			(int(*)(const void*, const void*))json_kv_pair_cmp);
}

static int json_kv_pair_cmp(const struct json_kv_pair_t *a, const struct json_kv_pair_t *b)
{
	return json_string_cmp(&a->name, &b->name);
}

void json_value_copy(const struct json_value_t *from, struct json_value_t *to)
{
	const struct json_kv_pair_t *from_field;
	struct json_kv_pair_t *to_field;
	const struct json_value_t *from_val;
	struct json_value_t *to_val;
	size_t i;

	json_value_destroy(to);
	memcpy(to, from, sizeof(*to));

	switch (from->type) {
		case JSON_OBJECT:
			to->object.fields = calloc(from->object.capacity, sizeof(*from_field));

			for (i = 0; i < from->object.length; i++) {
				from_field = &from->object.fields[i];
				to_field = &to->object.fields[i];

				json_string_copy(&from_field->name, &to_field->name);
				json_value_copy(from_field->value, to_field->value);
			}

			break;

		case JSON_ARRAY:
			to->array.values = calloc(from->object.capacity, sizeof(*from_val));

			for (i = 0; i < from->array.length; i++) {
				from_val = &from->array.values[i];
				to_val = &to->array.values[i];

				json_value_copy(from_val, to_val);
			}

			break;

		case JSON_STRING:
			to->string.text = malloc(from->string.length);
			memcpy(to->string.text, from->string.text, to->string.length);
			break;

		case JSON_INT:
		case JSON_FLOAT:
		case JSON_BOOL:
		case JSON_NULL:
			break;

		default:
			abort();
	}
}

void json_value_move(struct json_value_t *from, struct json_value_t *to)
{
	json_value_destroy(to);
	memcpy(to, from, sizeof(*from));
	memset(from, 0, sizeof(*from));
}

void json_value_destroy(struct json_value_t *v)
{
	size_t i;

	if (v->type == JSON_NONE)
		return;

	switch (v->type) {
		case JSON_OBJECT:
			for (i = 0; i < v->object.length; i++) {
				json_string_destroy(&v->object.fields[i].name);
				json_value_destroy(v->object.fields[i].value);
			}

			free(v->object.fields);
			break;

		case JSON_ARRAY:
			for (i = 0; i < v->array.length; i++)
				json_value_destroy(&v->array.values[i]);

			free(v->array.values);
			break;

		case JSON_STRING:
			free(v->string.text);
			break;

		case JSON_INT:
		case JSON_FLOAT:
		case JSON_BOOL:
		case JSON_NULL:
			break;

		default:
			abort();
	}

	memset(v, 0, sizeof(*v));
}

#define WRITE_LITERAL(__str) (sink_write)(sink, __str, sizeof(__str) - 1)

static void json_kv_to_str(
		const struct json_kv_pair_t *kv,
		void *sink,
		void (*sink_write)(void *sink, const char *text, size_t length));

static void json_string_to_str(
		const char *s,
		size_t length,
		void *sink,
		void (*sink_write)(void *sink, const char *text, size_t length));

void json_value_to_string(
		const struct json_value_t *v,
		void *sink,
		void (*sink_write)(void *sink, const char *text, size_t length))
{
	const struct json_kv_pair_t *kv;
	const struct json_value_t *value;
	size_t i;
	char numbuf[128];
	int chars_written;

	switch (v->type) {
		case JSON_OBJECT:
			WRITE_LITERAL("{");

			for (i = 0; i < v->object.length - 1 && v->object.length; i++) {
				kv = &v->object.fields[i];
				json_kv_to_str(kv, sink, sink_write);
				WRITE_LITERAL(", ");
			}

			if (v->object.length > 0)
				json_kv_to_str(&v->object.fields[i], sink, sink_write);

			WRITE_LITERAL("}");
			break;

		case JSON_ARRAY:
			WRITE_LITERAL("[");

			for (i = 0; i < v->array.length - 1 && v->array.length; i++) {
				value = &v->array.values[i];
				json_value_to_string(value, sink, sink_write);
				WRITE_LITERAL(", ");
			}

			if (v->array.length > 0)
				json_value_to_string(&v->array.values[i], sink, sink_write);

			WRITE_LITERAL("]");
			break;

		case JSON_STRING:
			json_string_to_str(v->string.text, v->string.length - 1, sink, sink_write);
			break;

		case JSON_INT:
			chars_written = snprintf(numbuf, sizeof(numbuf), "%" PRIi64, v->n_int);
			sink_write(sink, numbuf, chars_written);
			break;

		case JSON_FLOAT:
			chars_written = snprintf(numbuf, sizeof(numbuf), "%g", v->n_float);
			sink_write(sink, numbuf, chars_written);
			break;

		case JSON_BOOL:
			if (v->n_int) {
				WRITE_LITERAL("true");
			} else {
				WRITE_LITERAL("false");
			}
			break;

		case JSON_NULL:
			WRITE_LITERAL("null");
			break;
	}
}

static void json_kv_to_str(
		const struct json_kv_pair_t *kv,
		void *sink,
		void (*sink_write)(void *sink, const char *text, size_t length))
{
	json_string_to_str(kv->name.text, kv->name.length - 1, sink, sink_write);
	WRITE_LITERAL(": ");
	json_value_to_string(kv->value, sink, sink_write);
}

static void json_string_to_str(
		const char *s,
		size_t length,
		void *sink,
		void (*sink_write)(void *sink, const char *text, size_t length))
{
	WRITE_LITERAL("\"");
	sink_write(sink, s, length);
	WRITE_LITERAL("\"");
}
