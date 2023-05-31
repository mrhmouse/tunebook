#include <stdio.h>
#include <stdlib.h>

struct tunebook_number {
  int numerator_base, denominator_base, numerator_exp, denominator_exp;
};

struct tunebook_token {
  enum {
    TOKEN_ADD,
    TOKEN_AM,
    TOKEN_ATTACK,
    TOKEN_BASE,
    TOKEN_CHORD_END,
    TOKEN_CHORD_START,
    TOKEN_DECAY,
    TOKEN_DETUNE,
    TOKEN_FM,
    TOKEN_FRACTION,
    TOKEN_FRACTION_EXP,
    TOKEN_GROOVE,
    TOKEN_HZ,
    TOKEN_INSTRUMENT,
    TOKEN_INTEGER,
    TOKEN_MODULATE,
    TOKEN_RELEASE,
    TOKEN_REPEAT,
    TOKEN_ROOT,
    TOKEN_SAW,
    TOKEN_SECTION,
    TOKEN_SINE,
    TOKEN_SONG,
    TOKEN_SQUARE,
    TOKEN_STRING,
    TOKEN_SUB,
    TOKEN_SUSTAIN,
    TOKEN_TEMPO,
    TOKEN_TRIANGLE,
    TOKEN_TYPE,
    TOKEN_VOICE,
    TOKEN_VOLUME,
  } type;
  union {
    struct tunebook_number number;
    char *string;
  } as;
};

struct tunebook_instrument {
  char *name;
  int n_oscillators;
  struct tunebook_oscillator *oscillators;
};

struct tunebook_oscillator {
  char *name;
  enum { OSC_SINE, OSC_SAW, OSC_TRIANGLE, OSC_SQUARE } shape;
  struct tunebook_number attack, decay, sustain, release;
  int n_am_targets, n_fm_targets, n_add_targets, n_sub_targets;
  struct tunebook_oscillator
  *am_targets, *fm_targets, *add_targets, *sub_targets;
};

struct tunebook_song {
  char *name;
  struct tunebook_number tempo, root;
  int n_voices;
  struct tunebook_voice *voices;
};

struct tunebook_book {
  int n_instruments, n_songs;
  struct tunebook_instrument *instruments;
  struct tunebook_song *songs;
};

struct tunebook_voice {
  struct tunebook_instrument instrument;
  int n_commands;
  struct tunebook_voice_command *commands;
};

struct tunebook_voice_command {
  enum {
    VOICE_COMMAND_NOTE,
    VOICE_COMMAND_MODULATE,
    VOICE_COMMAND_GROOVE,
    VOICE_COMMAND_CHORD,
    VOICE_COMMAND_SECTION,
    VOICE_COMMAND_REPEAT,
  } type;
  union {
    struct tunebook_number note, modulate;
    struct {
      int n_notes;
      struct tunebook_number *notes;
    } groove, chord;
    int repeat;
  } as;
};

struct tunebook_error {
};

void tunebook_print_error(struct tunebook_error error) {
  fputs("uh oh stinky\n", stderr);
}

int tunebook_read_file
(FILE *in, struct tunebook_book *book, struct tunebook_error *error) {
}

int tunebook_write_song
(struct tunebook_song song, FILE *out, struct tunebook_error *error) {
}

int tunebook_write_book
(struct tunebook_book book, struct tunebook_error *error) {
}

int main() {
  struct tunebook_book book;
  struct tunebook_error error;
  if (tunebook_read_file(stdin, &book, &error)) goto error;
  if (tunebook_write_book(book, &error)) goto error;
  return 0;
 error:
  tunebook_print_error(error);
  return -1;
}
