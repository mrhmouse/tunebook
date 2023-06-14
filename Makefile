CFLAGS = -g -Wall -Werror
LIBS = -lm

tunebook: tunebook.c
	gcc $(CFLAGS) tunebook.c -o $@ $(LIBS)
