#include <stdio.h>

#include "fstream_reader.h"
#include "tokenizer.h"

int main(void)
{
	struct fstream_reader fstr;
	struct tokenizer tok;
	size_t i;

	fstream_init(&fstr, stdin, 4096);
	tok_init(&tok, &fstr, (int (*)(void *))fstream_next);

	for (i = 0; tok_next(&tok) == 1; i++)
		printf("#%zu (%s): \"%s\"\n", i, tok_kind_to_string(tok.kind), tok.text);

	tok_destroy(&tok);
	fstream_destroy(&fstr);

	return 0;
}
