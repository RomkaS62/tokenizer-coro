#include <stdio.h>

#include "fstream_reader.h"
#include "json.h"

static void write_to_file(FILE *f, const char *bytes, size_t length)
{
	fwrite(bytes, 1, length, f);
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

	for (i = 0; !json_value_parse(&tok, &val); i++) {
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
