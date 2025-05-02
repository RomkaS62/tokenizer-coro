#include <stdio.h>

#include "fstream_reader.h"
#include "json.h"

static void write_to_file(FILE *f, const char *bytes, size_t length)
{
	fwrite(bytes, 1, length, f);
}

static void report_error(void *, const char *unexpected_token, size_t length,
		size_t linenum, size_t char_pos)
{
	fprintf(stderr, "Unexpected token at %zu:%zu --- \"%s\"\n",
			linenum + 1, char_pos + 1, unexpected_token);
}

int main(void)
{
	struct fstream_reader fstr = { 0 };
	struct json_tokenizer_t tok = { 0 };
	struct json_value_t val = { 0 };
	size_t i = 0;
	int ret = 1;

	fstream_init(&fstr, stdin, 4096);
	json_tokenizer_init(&tok, &fstr, (int (*)(void *))fstream_next);
	json_tokenizer_next(&tok);

	tok.on_error = report_error;

	for (i = 0; tok.kind > 0; i++) {
		if (json_value_parse(&tok, &val))
			break;

		ret = 0;
		printf("Object #%zu: ", i);
		json_value_to_string(&val,  stdout,
				(void(*)(void *, const char *, size_t))write_to_file);
		puts("");
		json_value_destroy(&val);
	}

	json_value_destroy(&val);
	json_tokenizer_destroy(&tok);
	fstream_destroy(&fstr);

	return ret;
}
