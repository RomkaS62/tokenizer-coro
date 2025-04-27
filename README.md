# Tokenizer with coroutines

This is a toy project meant to demostrate the virtues of coroutines in C. The
program written here reads a stream of text and prints it out as a list of
simple tokens without reading the entire text into memory.

## How to build?

In the simplest possible manner:

	mkdir build
	cd build
	cmake ../
	make
