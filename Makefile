CFLAGS = -Wall -O3
LIBS = -lm

tunebook: tunebook.c
	gcc $(CFLAGS) tunebook.c -o $@ $(LIBS)
