#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NEW(target, size) target = malloc((size) * sizeof *target)
#define RESIZE(target, size) target = realloc(target, (size) * sizeof *target)

struct tunebook_number {
  enum { NUMBER_RATIONAL, NUMBER_EXPONENTIAL } type;
  int numerator, denominator;
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
    TOKEN_GROOVE,
    TOKEN_HZ,
    TOKEN_INSTRUMENT,
    TOKEN_MODULATE,
    TOKEN_NUMBER,
    TOKEN_RELEASE,
    TOKEN_REPEAT,
    TOKEN_REST,
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
  struct tunebook_number attack, decay, sustain, release, volume, hz, detune;
  int n_am_targets, n_fm_targets, n_add_targets, n_sub_targets;
  char **am_targets, **fm_targets, **add_targets, **sub_targets;
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
  char *instrument;
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
    VOICE_COMMAND_REST,
  } type;
  union {
    struct tunebook_number note, modulate, repeat;
    struct {
      int n_notes;
      struct tunebook_number *notes;
    } groove, chord;
  } as;
};

struct tunebook_error {
  enum {
    ERROR_EOF,
    ERROR_UNIMPLEMENTED,
    ERROR_EXPECTED_STRING,
    ERROR_EXPECTED_NUMBER,
    ERROR_EXPECTED_CHORD_START,
    ERROR_UNKNOWN_INSTRUMENT,
    ERROR_UNKNOWN_KEYWORD,
  } type;
  struct tunebook_token last_token;
};

void tunebook_print_error(struct tunebook_error error) {
  fprintf(stderr, "uh oh stinky: %i %i\n", error.type, error.last_token.type);
}

int tunebook_next_token
(FILE *in, struct tunebook_token *token, struct tunebook_error *error) {
  int c, n_buffer, s_buffer, sign = 1;
  char *buffer;
  do c = fgetc(in); while (isspace(c));
  if (c == EOF) {
    error->type = ERROR_EOF;
    return -1;
  }
  switch (c) {
  case '(':
    token->type = TOKEN_CHORD_START;
    return 0;
  case ')':
    token->type = TOKEN_CHORD_END;
    return 0;
  case '"':
    n_buffer = 0;
    s_buffer = 32;
    NEW(buffer, s_buffer);
    for (;;) {
      c = fgetc(in);
      if (c == '"') break;
      if (++n_buffer >= s_buffer) {
	s_buffer *= 2;
	RESIZE(buffer, s_buffer);
      }
      buffer[n_buffer-1] = c;
    }
    buffer[n_buffer] = 0;
    RESIZE(buffer, n_buffer);
    token->type = TOKEN_STRING;
    token->as.string = buffer;
    return 0;
  case '-':
    sign = -1;
    c = '0';
    break;
  }
  if (isdigit(c)) {
    token->type = TOKEN_NUMBER;
    token->as.number = (struct tunebook_number){ NUMBER_RATIONAL, c-'0', 1 };
    for (;;) {
      c = fgetc(in);
      if (!isdigit(c)) break;
      token->as.number.numerator *= 10;
      token->as.number.numerator += c-'0';
    }
    token->as.number.numerator *= sign;
    if (c == '/' || c == '\\') {
      if (c == '\\') token->as.number.type = NUMBER_EXPONENTIAL;
      token->as.number.denominator = 0;
      for (;;) {
	c = fgetc(in);
	if (!isdigit(c)) break;
	token->as.number.denominator *= 10;
	token->as.number.denominator += c-'0';
      }
    }
    ungetc(c, in);
    return 0;
  }
  // parse symbol
  n_buffer = 1;
  s_buffer = 32;
  NEW(buffer, s_buffer);
  buffer[0] = c;
  for (;;) {
    c = fgetc(in);
    if (isspace(c)) break;
    if (++n_buffer >= s_buffer) {
      s_buffer *= 2;
      RESIZE(buffer, s_buffer);
    }
    buffer[n_buffer-1] = c;
  }
  buffer[n_buffer] = 0;
  RESIZE(buffer, n_buffer);
  if (!strcmp(buffer, "add")) token->type = TOKEN_ADD;
  else if (!strcmp(buffer, "am")) token->type = TOKEN_AM;
  else if (!strcmp(buffer, "attack")) token->type = TOKEN_ATTACK;
  else if (!strcmp(buffer, "base")) token->type = TOKEN_BASE;
  else if (!strcmp(buffer, "decay")) token->type = TOKEN_DECAY;
  else if (!strcmp(buffer, "detune")) token->type = TOKEN_DETUNE;
  else if (!strcmp(buffer, "fm")) token->type = TOKEN_FM;
  else if (!strcmp(buffer, "groove")) token->type = TOKEN_GROOVE;
  else if (!strcmp(buffer, "hz")) token->type = TOKEN_HZ;
  else if (!strcmp(buffer, "instrument")) token->type = TOKEN_INSTRUMENT;
  else if (!strcmp(buffer, "modulate")) token->type = TOKEN_MODULATE;
  else if (!strcmp(buffer, "release")) token->type = TOKEN_RELEASE;
  else if (!strcmp(buffer, "repeat")) token->type = TOKEN_REPEAT;
  else if (!strcmp(buffer, "r")) token->type = TOKEN_REST;
  else if (!strcmp(buffer, "rest")) token->type = TOKEN_REST;
  else if (!strcmp(buffer, "root")) token->type = TOKEN_ROOT;
  else if (!strcmp(buffer, "saw")) token->type = TOKEN_SAW;
  else if (!strcmp(buffer, "section")) token->type = TOKEN_SECTION;
  else if (!strcmp(buffer, "sine")) token->type = TOKEN_SINE;
  else if (!strcmp(buffer, "song")) token->type = TOKEN_SONG;
  else if (!strcmp(buffer, "square")) token->type = TOKEN_SQUARE;
  else if (!strcmp(buffer, "sub")) token->type = TOKEN_SUB;
  else if (!strcmp(buffer, "sustain")) token->type = TOKEN_SUSTAIN;
  else if (!strcmp(buffer, "tempo")) token->type = TOKEN_TEMPO;
  else if (!strcmp(buffer, "tri")) token->type = TOKEN_TRIANGLE;
  else if (!strcmp(buffer, "triangle")) token->type = TOKEN_TRIANGLE;
  else if (!strcmp(buffer, "voice")) token->type = TOKEN_VOICE;
  else if (!strcmp(buffer, "volume")) token->type = TOKEN_VOLUME;
  else {
    error->type = ERROR_UNKNOWN_KEYWORD;
    free(buffer);
    return -1;
  }
  free(buffer);
  return 0;
}

int tunebook_read_file
(FILE *in, struct tunebook_book *book, struct tunebook_error *error) {
  struct tunebook_token token;
  int s_instruments = 8, s_songs = 8,
    command_type, shape, s_voices, s_oscillators, s_am_targets,
    s_fm_targets, s_add_targets, s_sub_targets, s_commands, s_notes;
#define INSTRUMENT book->instruments[book->n_instruments-1]
#define OSCILLATOR INSTRUMENT.oscillators[INSTRUMENT.n_oscillators-1]
#define SONG book->songs[book->n_songs-1]
#define VOICE SONG.voices[SONG.n_voices-1]
#define COMMAND VOICE.commands[VOICE.n_commands-1]
  book->n_instruments = 0;
  book->n_songs = 0;
  NEW(book->instruments, s_instruments);
  NEW(book->songs, s_songs);
  for (;;) {
    if (tunebook_next_token(in, &token, error)) goto error;
    switch (token.type) {
    case TOKEN_INSTRUMENT:
      if (++book->n_instruments >= s_instruments) {
	s_instruments *= 2;
	RESIZE(book->instruments, s_instruments);
      }
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_STRING) {
	error->type = ERROR_EXPECTED_STRING;
	goto error;
      }
      s_oscillators = 4;
      shape = OSC_SINE;
      INSTRUMENT.name = token.as.string;
      INSTRUMENT.n_oscillators = 0;
      NEW(INSTRUMENT.oscillators, s_oscillators);
      break;
    case TOKEN_SQUARE:
      shape = OSC_SQUARE;
      goto oscillator;
    case TOKEN_TRIANGLE:
      shape = OSC_TRIANGLE;
      goto oscillator;
    case TOKEN_SAW:
      shape = OSC_SAW;
      goto oscillator;
    case TOKEN_SINE:
      shape = OSC_SINE;
    oscillator:
      if (++INSTRUMENT.n_oscillators >= s_oscillators) {
	s_oscillators *= 2;
	RESIZE(INSTRUMENT.oscillators, s_oscillators);
      }
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_STRING) {
	error->type = ERROR_EXPECTED_STRING;
	goto error;
      }
      s_am_targets = 2;
      s_fm_targets = 2;
      s_add_targets = 2;
      s_sub_targets = 2;
      OSCILLATOR.name = token.as.string;
      OSCILLATOR.n_am_targets = 0;
      OSCILLATOR.n_fm_targets = 0;
      OSCILLATOR.n_add_targets = 0;
      OSCILLATOR.n_sub_targets = 0;
      NEW(OSCILLATOR.am_targets, s_am_targets);
      NEW(OSCILLATOR.fm_targets, s_fm_targets);
      NEW(OSCILLATOR.add_targets, s_add_targets);
      NEW(OSCILLATOR.sub_targets, s_sub_targets);
      OSCILLATOR.shape = shape;
      OSCILLATOR.attack = (struct tunebook_number){ NUMBER_RATIONAL, 1, 32 };
      OSCILLATOR.decay = (struct tunebook_number){ NUMBER_RATIONAL, 1, 3 };
      OSCILLATOR.sustain = (struct tunebook_number){ NUMBER_RATIONAL, 3, 4 };
      OSCILLATOR.release = (struct tunebook_number){ NUMBER_RATIONAL, 1, 32 };
      OSCILLATOR.volume = (struct tunebook_number){ NUMBER_RATIONAL, 1, 2 };
      OSCILLATOR.hz = (struct tunebook_number){ NUMBER_RATIONAL, 0, 0 };
      OSCILLATOR.detune = (struct tunebook_number){ NUMBER_RATIONAL, 1, 1 };
      break;
    case TOKEN_DETUNE:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.detune = token.as.number;
      break;
    case TOKEN_HZ:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.hz = token.as.number;
      break;
    case TOKEN_ATTACK:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.attack = token.as.number;
      break;
    case TOKEN_DECAY:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.decay = token.as.number;
      break;
    case TOKEN_SUSTAIN:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.sustain = token.as.number;
      break;
    case TOKEN_RELEASE:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.release = token.as.number;
      break;
    case TOKEN_VOLUME:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.volume = token.as.number;
      break;
    case TOKEN_AM:
      if (++OSCILLATOR.n_am_targets >= s_am_targets) {
	s_am_targets *= 2;
	RESIZE(OSCILLATOR.am_targets, s_am_targets);
      }
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_CHORD_START) {
	error->type = ERROR_EXPECTED_CHORD_START;
	goto error;
      }
      for (;;) {
	if (tunebook_next_token(in, &token, error)) goto error;
	if (token.type == TOKEN_CHORD_END) break;
	if (token.type != TOKEN_STRING) {
	  error->type = ERROR_EXPECTED_STRING;
	  goto error;
	}
	if (++OSCILLATOR.n_am_targets >= s_am_targets) {
	  s_am_targets *= 2;
	  RESIZE(OSCILLATOR.am_targets, s_am_targets);
	}
	OSCILLATOR.am_targets[OSCILLATOR.n_am_targets-1] = token.as.string;
      }
      break;
    case TOKEN_FM:
      if (++OSCILLATOR.n_fm_targets >= s_fm_targets) {
	s_fm_targets *= 2;
	RESIZE(OSCILLATOR.fm_targets, s_fm_targets);
      }
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_CHORD_START) {
	error->type = ERROR_EXPECTED_CHORD_START;
	goto error;
      }
      for (;;) {
	if (tunebook_next_token(in, &token, error)) goto error;
	if (token.type == TOKEN_CHORD_END) break;
	if (token.type != TOKEN_STRING) {
	  error->type = ERROR_EXPECTED_STRING;
	  goto error;
	}
	if (++OSCILLATOR.n_fm_targets >= s_fm_targets) {
	  s_fm_targets *= 2;
	  RESIZE(OSCILLATOR.fm_targets, s_fm_targets);
	}
	OSCILLATOR.fm_targets[OSCILLATOR.n_fm_targets-1] = token.as.string;
      }
      break;
    case TOKEN_ADD:
      if (++OSCILLATOR.n_add_targets >= s_add_targets) {
	s_add_targets *= 2;
	RESIZE(OSCILLATOR.add_targets, s_add_targets);
      }
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_CHORD_START) {
	error->type = ERROR_EXPECTED_CHORD_START;
	goto error;
      }
      for (;;) {
	if (tunebook_next_token(in, &token, error)) goto error;
	if (token.type == TOKEN_CHORD_END) break;
	if (token.type != TOKEN_STRING) {
	  error->type = ERROR_EXPECTED_STRING;
	  goto error;
	}
	if (++OSCILLATOR.n_add_targets >= s_add_targets) {
	  s_add_targets *= 2;
	  RESIZE(OSCILLATOR.add_targets, s_add_targets);
	}
	OSCILLATOR.add_targets[OSCILLATOR.n_add_targets-1] = token.as.string;
      }
      break;
    case TOKEN_SUB:
      if (++OSCILLATOR.n_sub_targets >= s_sub_targets) {
	s_sub_targets *= 2;
	RESIZE(OSCILLATOR.sub_targets, s_sub_targets);
      }
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_CHORD_START) {
	error->type = ERROR_EXPECTED_CHORD_START;
	goto error;
      }
      for (;;) {
	if (tunebook_next_token(in, &token, error)) goto error;
	if (token.type == TOKEN_CHORD_END) break;
	if (token.type != TOKEN_STRING) {
	  error->type = ERROR_EXPECTED_STRING;
	  goto error;
	}
	if (++OSCILLATOR.n_sub_targets >= s_sub_targets) {
	  s_sub_targets *= 2;
	  RESIZE(OSCILLATOR.sub_targets, s_sub_targets);
	}
	OSCILLATOR.sub_targets[OSCILLATOR.n_sub_targets-1] = token.as.string;
      }
      break;
    case TOKEN_SONG:
      if (++book->n_songs >= s_songs) {
	s_songs *= 2;
	RESIZE(book->songs, s_songs);
      }
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_STRING) {
	error->type = ERROR_EXPECTED_STRING;
	goto error;
      }
      s_voices = 8;
      SONG.name = token.as.string;
      SONG.tempo = (struct tunebook_number){ NUMBER_RATIONAL, 60, 1 };
      SONG.root = (struct tunebook_number){ NUMBER_RATIONAL, 440, 1 };
      SONG.n_voices = 0;
      NEW(SONG.voices, s_voices);
      break;
    case TOKEN_TEMPO:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      SONG.tempo = token.as.number;
      break;
    case TOKEN_ROOT:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      SONG.root = token.as.number;
      break;
    case TOKEN_VOICE:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_STRING) {
	error->type = ERROR_EXPECTED_STRING;
	goto error;
      }
      if (++SONG.n_voices >= s_voices) {
	s_voices *= 2;
	RESIZE(SONG.voices, s_voices);
      }
      s_commands = 32;
      VOICE.instrument = token.as.string;
      VOICE.n_commands = 0;
      NEW(VOICE.commands, s_commands);
      break;
    case TOKEN_GROOVE:
      if (++VOICE.n_commands >= s_commands) {
	s_commands *= 2;
	  RESIZE(VOICE.commands, s_commands);
	}
      s_notes = 4;
      COMMAND.type = VOICE_COMMAND_GROOVE;
      COMMAND.as.groove.n_notes = 0;
      NEW(COMMAND.as.groove.notes, s_notes);
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_CHORD_START) {
	error->type = ERROR_EXPECTED_CHORD_START;
	goto error;
      }
      for (;;) {
	if (tunebook_next_token(in, &token, error)) goto error;
	if (token.type == TOKEN_CHORD_END) break;
	if (token.type != TOKEN_NUMBER) {
	  error->type = ERROR_EXPECTED_NUMBER;
	  goto error;
	}
	if (++COMMAND.as.groove.n_notes >= s_notes) {
	  s_notes *= 2;
	  RESIZE(COMMAND.as.groove.notes, s_notes);
	}
	COMMAND.as.groove.notes[COMMAND.as.groove.n_notes-1] = token.as.number;
      }
      break;
    case TOKEN_CHORD_START:
      if (++VOICE.n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(VOICE.commands, s_commands);
      }
      s_notes = 4;
      COMMAND.type = VOICE_COMMAND_CHORD;
      COMMAND.as.chord.n_notes = 0;
      NEW(COMMAND.as.chord.notes, s_notes);
      if (tunebook_next_token(in, &token, error)) goto error;
      for (;;) {
	if (tunebook_next_token(in, &token, error)) goto error;
	if (token.type == TOKEN_CHORD_END) break;
	if (token.type != TOKEN_NUMBER) {
	  error->type = ERROR_EXPECTED_NUMBER;
	  goto error;
	}
	if (++COMMAND.as.chord.n_notes >= s_notes) {
	  s_notes *= 2;
	  RESIZE(COMMAND.as.chord.notes, s_notes);
	}
	COMMAND.as.chord.notes[COMMAND.as.chord.n_notes-1] = token.as.number;
      }
      break;
    case TOKEN_NUMBER:
      if (++VOICE.n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(VOICE.commands, s_commands);
      }
      COMMAND.type = VOICE_COMMAND_NOTE;
      COMMAND.as.note = token.as.number;
      break;
    case TOKEN_SECTION:
      if (++VOICE.n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(VOICE.commands, s_commands);
      }
      COMMAND.type = VOICE_COMMAND_SECTION;
      break;
    case TOKEN_REPEAT:
      if (++VOICE.n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(VOICE.commands, s_commands);
      }
      COMMAND.type = VOICE_COMMAND_REPEAT;
      COMMAND.as.repeat = token.as.number;
      break;
    case TOKEN_REST:
      if (++VOICE.n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(VOICE.commands, s_commands);
      }
      COMMAND.type = VOICE_COMMAND_REST;
      break;
    case TOKEN_MODULATE:
      if (++VOICE.n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(VOICE.commands, s_commands);
      }
      COMMAND.type = VOICE_COMMAND_MODULATE;
      COMMAND.as.modulate = token.as.number;
      break;
    default:
      error->type = ERROR_UNIMPLEMENTED;
      error->last_token = token;
      goto error;
    }
  }
 error:
  // TODO cleanup excess memory
  if (error->type == ERROR_EOF) return 0;
  return -1;
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
  puts("READ FILE");
  return 0;
  if (tunebook_write_book(book, &error)) goto error;
  return 0;
 error:
  tunebook_print_error(error);
  return -1;
}
