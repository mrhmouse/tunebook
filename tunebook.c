#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/random.h>
#define SAMPLE int16_t
#define SAMPLE_MAX INT16_MAX
#define SAMPLE_RATE 48000
#define NOISE_SEED 0xdeadbeef
#define MAX_NOISE_STEPS SAMPLE_RATE
#define NEW(target, size) target = malloc((size) * sizeof *target)
#define RESIZE(target, size) target = realloc(target, (size) * sizeof *target)

typedef double (* osc_fun)(double);

double square(double i) {
  return 2*(floor(sin(i)) + 0.5);
}

double saw(double i) {
  i *= .25;
  return (2 * (i - trunc(i))) - 1;
}

double triangle(double i) {
  return (2 * fabs(saw(i))) - 1;
}

static double *noise_buffer = NULL;
double noise(double i) {
  if (!noise_buffer) {
    srandom(NOISE_SEED);
    NEW(noise_buffer, MAX_NOISE_STEPS);
    long sum = 0;
    for (int i = 0; i < MAX_NOISE_STEPS; ++i) {
      sum += random();
      noise_buffer[i] = sin(sum);
    }
  }
  return noise_buffer[abs(i) % MAX_NOISE_STEPS];
}

struct tunebook_number {
  enum { NUMBER_RATIONAL, NUMBER_EXPONENTIAL } type;
  int numerator, denominator;
};

struct tunebook_chord {
  int n_notes;
  struct tunebook_number *notes;
};

struct tunebook_token {
  enum {
    TOKEN_ADD, TOKEN_AM, TOKEN_ATTACK,
    TOKEN_BASE, TOKEN_CLIP, TOKEN_CHORD_END,
    TOKEN_CHORD_START, TOKEN_DECAY, TOKEN_DETUNE,
    TOKEN_ENV, TOKEN_FM, TOKEN_PM, TOKEN_GROOVE, TOKEN_HZ,
    TOKEN_INSTRUMENT, TOKEN_MODULATE, TOKEN_NOISE,
    TOKEN_NUMBER, TOKEN_RELEASE, TOKEN_REPEAT, TOKEN_REST,
    TOKEN_ROOT, TOKEN_SAW, TOKEN_SECTION, TOKEN_SINE,
    TOKEN_SONG, TOKEN_SQUARE, TOKEN_STRING,
    TOKEN_SUB, TOKEN_SUSTAIN, TOKEN_TEMPO,
    TOKEN_TRIANGLE, TOKEN_VOICE, TOKEN_VOLUME,
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
  enum { OSC_SINE, OSC_SAW, OSC_TRIANGLE, OSC_SQUARE, OSC_NOISE } shape;
  double attack, decay, sustain, release, volume, hz, detune, clip;
  int n_am_targets, n_fm_targets, n_pm_targets, n_add_targets, n_sub_targets, n_env_targets;
  char **am_targets, **fm_targets, **pm_targets, **add_targets, **sub_targets, **env_targets;
};

struct tunebook_song {
  char *name;
  double tempo, root;
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
    VOICE_COMMAND_BASE, VOICE_COMMAND_CHORD,
    VOICE_COMMAND_GROOVE, VOICE_COMMAND_MODULATE,
    VOICE_COMMAND_NOTE, VOICE_COMMAND_REPEAT,
    VOICE_COMMAND_REST, VOICE_COMMAND_SECTION,
  } type;
  union {
    struct tunebook_number base, note, modulate, repeat;
    struct tunebook_chord groove, chord;
  } as;
};

struct tunebook_error {
  enum {
    ERROR_EOF, ERROR_UNIMPLEMENTED, ERROR_EXPECTED_STRING,
    ERROR_EXPECTED_NUMBER, ERROR_EXPECTED_CHORD_START,
    ERROR_UNKNOWN_INSTRUMENT, ERROR_UNKNOWN_KEYWORD
  } type;
  struct tunebook_token last_token;
};

double number_to_double(double base, struct tunebook_number coeffecient) {
  double c = (double)coeffecient.numerator / coeffecient.denominator;
  if (coeffecient.type == NUMBER_EXPONENTIAL) return pow(base, c);
  else return c;
}

void tunebook_print_error(struct tunebook_error error) {
  fprintf(stderr, "uh oh stinky: %i %i\n", error.type, error.last_token.type);
}

int tunebook_next_token
(FILE *in, struct tunebook_token *token, struct tunebook_error *error) {
  int c, n_buffer, s_buffer, sign = 1;
  char *buffer;
 retry:
  do c = fgetc(in); while (isspace(c));
  if (c == EOF) {
    error->type = ERROR_EOF;
    return -1;
  }
  switch (c) {
  case '#':
    do c = fgetc(in); while (c != '\n');
    goto retry;
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
  else if (!strcmp(buffer, "clip")) token->type = TOKEN_CLIP;
  else if (!strcmp(buffer, "decay")) token->type = TOKEN_DECAY;
  else if (!strcmp(buffer, "detune")) token->type = TOKEN_DETUNE;
  else if (!strcmp(buffer, "env")) token->type = TOKEN_ENV;
  else if (!strcmp(buffer, "fm")) token->type = TOKEN_FM;
  else if (!strcmp(buffer, "groove")) token->type = TOKEN_GROOVE;
  else if (!strcmp(buffer, "hz")) token->type = TOKEN_HZ;
  else if (!strcmp(buffer, "instrument")) token->type = TOKEN_INSTRUMENT;
  else if (!strcmp(buffer, "modulate")) token->type = TOKEN_MODULATE;
  else if (!strcmp(buffer, "noise")) token->type = TOKEN_NOISE;
  else if (!strcmp(buffer, "pm")) token->type = TOKEN_PM;
  else if (!strcmp(buffer, "release")) token->type = TOKEN_RELEASE;
  else if (!strcmp(buffer, "repeat")) token->type = TOKEN_REPEAT;
  else if (!strcmp(buffer, "r")) token->type = TOKEN_REST;
  else if (!strcmp(buffer, "rest")) token->type = TOKEN_REST;
  else if (!strcmp(buffer, "root")) token->type = TOKEN_ROOT;
  else if (!strcmp(buffer, "saw")) token->type = TOKEN_SAW;
  else if (!strcmp(buffer, "section")) token->type = TOKEN_SECTION;
  else if (!strcmp(buffer, "sin")) token->type = TOKEN_SINE;
  else if (!strcmp(buffer, "sine")) token->type = TOKEN_SINE;
  else if (!strcmp(buffer, "song")) token->type = TOKEN_SONG;
  else if (!strcmp(buffer, "sqr")) token->type = TOKEN_SQUARE;
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
    shape, s_voices = 0, s_oscillators = 0, s_am_targets = 0,
    s_fm_targets = 0, s_pm_targets = 0, s_add_targets = 0,
    s_sub_targets = 0, s_env_targets = 0, s_commands = 0, s_notes = 0;
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
    case TOKEN_BASE:
      if (++VOICE.n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(VOICE.commands, s_commands);
      }
      COMMAND.type = VOICE_COMMAND_BASE;
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      COMMAND.as.base = token.as.number;
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
    case TOKEN_NOISE:
      shape = OSC_NOISE;
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
      s_pm_targets = 2;
      s_add_targets = 2;
      s_sub_targets = 2;
      s_env_targets = 2;
      OSCILLATOR.name = token.as.string;
      OSCILLATOR.n_am_targets = 0;
      OSCILLATOR.n_fm_targets = 0;
      OSCILLATOR.n_pm_targets = 0;
      OSCILLATOR.n_add_targets = 0;
      OSCILLATOR.n_sub_targets = 0;
      OSCILLATOR.n_env_targets = 0;
      NEW(OSCILLATOR.am_targets, s_am_targets);
      NEW(OSCILLATOR.fm_targets, s_fm_targets);
      NEW(OSCILLATOR.pm_targets, s_pm_targets);
      NEW(OSCILLATOR.add_targets, s_add_targets);
      NEW(OSCILLATOR.sub_targets, s_sub_targets);
      NEW(OSCILLATOR.env_targets, s_env_targets);
      OSCILLATOR.shape = shape;
      OSCILLATOR.attack = 1.0/32.0;
      OSCILLATOR.clip = 0;
      OSCILLATOR.decay = 1.0/3.0;
      OSCILLATOR.sustain = 3.0/4.0;
      OSCILLATOR.release = 1.0/32.0;
      OSCILLATOR.volume = 1.0/2.0;
      OSCILLATOR.hz = 0;
      OSCILLATOR.detune = 1;
      break;
    case TOKEN_CLIP:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.clip = number_to_double(1, token.as.number);
      break;
    case TOKEN_DETUNE:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.detune = number_to_double(1, token.as.number);
      break;
    case TOKEN_ENV:
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
	if (++OSCILLATOR.n_env_targets >= s_env_targets) {
	  s_env_targets *= 2;
	  RESIZE(OSCILLATOR.env_targets, s_env_targets);
	}
	OSCILLATOR.env_targets[OSCILLATOR.n_env_targets-1] = token.as.string;
      }
      break;
    case TOKEN_HZ:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.hz = number_to_double(1, token.as.number);
      break;
    case TOKEN_ATTACK:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.attack = number_to_double(1, token.as.number);
      break;
    case TOKEN_DECAY:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.decay = number_to_double(1, token.as.number);
      break;
    case TOKEN_SUSTAIN:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.sustain = number_to_double(1, token.as.number);
      break;
    case TOKEN_RELEASE:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.release = number_to_double(1, token.as.number);
      break;
    case TOKEN_VOLUME:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.volume = number_to_double(1, token.as.number);
      break;
    case TOKEN_AM:
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
    case TOKEN_PM:
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
	if (++OSCILLATOR.n_pm_targets >= s_pm_targets) {
	  s_pm_targets *= 2;
	  RESIZE(OSCILLATOR.pm_targets, s_pm_targets);
	}
	OSCILLATOR.pm_targets[OSCILLATOR.n_pm_targets-1] = token.as.string;
      }
      break;
    case TOKEN_ADD:
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
      SONG.tempo = 60;
      SONG.root = 440;
      SONG.n_voices = 0;
      NEW(SONG.voices, s_voices);
      break;
    case TOKEN_TEMPO:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      SONG.tempo = number_to_double(1, token.as.number);
      break;
    case TOKEN_ROOT:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      SONG.root = number_to_double(1, token.as.number);
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
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
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
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      COMMAND.as.modulate = token.as.number;
      break;
    default:
      error->type = ERROR_UNIMPLEMENTED;
      error->last_token = token;
      goto error;
    }
  }
  RESIZE(book->instruments, book->n_instruments);
  RESIZE(book->songs, book->n_songs);
  for (int i = 0; i < book->n_instruments; ++i) {
    RESIZE(book->instruments[i].oscillators, book->instruments[i].n_oscillators);
    for (int o = 0; o < book->instruments[i].n_oscillators; ++o) {
      RESIZE(book->instruments[i].oscillators[o].am_targets,
	     book->instruments[i].oscillators[o].n_am_targets);
      RESIZE(book->instruments[i].oscillators[o].fm_targets,
	     book->instruments[i].oscillators[o].n_fm_targets);
      RESIZE(book->instruments[i].oscillators[o].pm_targets,
	     book->instruments[i].oscillators[o].n_pm_targets);
      RESIZE(book->instruments[i].oscillators[o].add_targets,
	     book->instruments[i].oscillators[o].n_add_targets);
      RESIZE(book->instruments[i].oscillators[o].sub_targets,
	     book->instruments[i].oscillators[o].n_sub_targets);
    }
  }
  for (int s = 0; s < book->n_songs; ++s) {
    RESIZE(book->songs[s].voices, book->songs[s].n_voices);
    for (int v = 0; v < book->songs[s].n_voices; ++v) {
      RESIZE(book->songs[s].voices[v].commands,
	     book->songs[s].voices[v].n_commands);
      for (int c = 0; c < book->songs[s].voices[v].n_commands; ++c) {
	switch (book->songs[s].voices[v].commands[c].type) {
	case VOICE_COMMAND_GROOVE:
	  RESIZE(book->songs[s].voices[v].commands[c].as.groove.notes,
		 book->songs[s].voices[v].commands[c].as.groove.n_notes);
	  break;
	case VOICE_COMMAND_CHORD:
	  RESIZE(book->songs[s].voices[v].commands[c].as.chord.notes,
		 book->songs[s].voices[v].commands[c].as.chord.n_notes);
	  break;
	default:
	  break;
	}
      }
    }
  }
  return 0;
 error:
  error->last_token = token;
  if (error->type == ERROR_EOF) return 0;
  return -1;
}

int is_modulator(struct tunebook_instrument *instrument, int o) {
  struct tunebook_oscillator *osc = &instrument->oscillators[o];
  return osc->n_am_targets
    + osc->n_fm_targets
    + osc->n_pm_targets
    + osc->n_add_targets
    + osc->n_sub_targets
    + osc->n_env_targets;
}

double amp_at_point
(int point, int beat_length, double freq,
 struct tunebook_instrument *instrument, int osc_i) {
  osc_fun wave_func = sin;
  struct tunebook_oscillator *osc = &instrument->oscillators[osc_i];
  int n_env = 0;
  int attack = osc->attack * beat_length;
  int decay = attack + (osc->decay * (double)beat_length);
  int release = beat_length;
  int length = beat_length * (1 + osc->release);
  switch (osc->shape) {
  case OSC_SAW: wave_func = saw; break;
  case OSC_TRIANGLE: wave_func = triangle; break;
  case OSC_SQUARE: wave_func = square; break;
  case OSC_SINE: wave_func = sin; break;
  case OSC_NOISE: wave_func = noise; break;
  }
  double fm = 0, am = 0, pm = 0, add = 0, sub = 0, env = 0;
  for (int i = 0; i < instrument->n_oscillators; ++i) {
    if (i == osc_i) continue;
    for (int j = 0; j < instrument->oscillators[i].n_am_targets; ++j) {
      if (strcmp(osc->name, instrument->oscillators[i].am_targets[j])) continue;
      am += amp_at_point(point, beat_length, freq, instrument, i);
    }
    for (int j = 0; j < instrument->oscillators[i].n_fm_targets; ++j) {
      if (strcmp(osc->name, instrument->oscillators[i].fm_targets[j])) continue;
      fm += amp_at_point(point, beat_length, freq, instrument, i);
    }
    for (int j = 0; j < instrument->oscillators[i].n_pm_targets; ++j) {
      if (strcmp(osc->name, instrument->oscillators[i].pm_targets[j])) continue;
      pm += amp_at_point(point, beat_length, freq, instrument, i);
    }
    for (int j = 0; j < instrument->oscillators[i].n_add_targets; ++j) {
      if (strcmp(osc->name, instrument->oscillators[i].add_targets[j])) continue;
      add += amp_at_point(point, beat_length, freq, instrument, i);
    }
    for (int j = 0; j < instrument->oscillators[i].n_sub_targets; ++j) {
      if (strcmp(osc->name, instrument->oscillators[i].sub_targets[j])) continue;
      sub += amp_at_point(point, beat_length, freq, instrument, i);
    }
    for (int j = 0; j < instrument->oscillators[i].n_env_targets; ++j) {
      if (strcmp(osc->name, instrument->oscillators[i].env_targets[j])) continue;
      n_env++;
      env += amp_at_point(point, beat_length, freq, instrument, i);
    }
  }
  freq *= osc->detune;
  if (osc->hz) freq = osc->hz;
  double amp = (1 + am) * osc->volume * wave_func((point + pm) * (freq * 1 + fm) * 2 * M_PI / SAMPLE_RATE);
  amp += add;
  amp -= sub;
  if (n_env) {
    if (env < 0) amp = MAX(env, MIN(0, amp));
    else amp = MIN(env, MAX(0, amp));
  }
  if (point < attack) {
    if (attack) amp *= (double)point/attack;
  } else if (point < decay) {
    if (decay != attack) {
      double q = (double)(point-attack)/(decay-attack);
      amp *= 1 - (q * (1 - osc->sustain));
    }
  } else if (point < release) {
    amp *= osc->sustain;
  } else {
    double q = (double)point/(length+release);
    amp *= (1 - q) * osc->sustain;
  }
  if (osc->clip > 0 && fabs(amp) > osc->clip) {
    amp = copysign(osc->clip, amp);
  }
  return amp;
}

void write_note
(FILE *out_file, int beat_length, double freq,
 struct tunebook_instrument *instrument, int osc_i) {
  SAMPLE sample;
  int length = beat_length * (1 + instrument->oscillators[osc_i].release);
  for (int i = 0; i < length; ++i) {
    if (0 == fread(&sample, sizeof sample, 1, out_file)) sample = 0;
    else fseek(out_file, -sizeof sample, SEEK_CUR);
    double amp = amp_at_point(i, beat_length, freq, instrument, osc_i);
    if (amp > 1) amp = 1;
    if (amp < -1) amp = -1;
    sample += SAMPLE_MAX * amp;
    fwrite(&sample, sizeof sample, 1, out_file);
  }
  fseek(out_file, (sizeof sample) * (beat_length-length), SEEK_CUR);
}

struct tunebook_render_context {
  int beat, osc, n_sections, s_sections, *sections;
  double base, root, tempo;
  struct tunebook_chord *groove;
};

void process_command
(struct tunebook_render_context *cx,
 FILE *out_file,
 struct tunebook_instrument *instrument,
 struct tunebook_voice *voice,
 int command_i) {
  SAMPLE sample;
  int length, current_repeat;
  struct tunebook_voice_command *command = &voice->commands[command_i];
  switch (command->type) {
  case VOICE_COMMAND_BASE:
    cx->base = number_to_double(cx->base, command->as.base);
    break;
  case VOICE_COMMAND_MODULATE:
    cx->root *= number_to_double(cx->base, command->as.modulate);
    break;
  case VOICE_COMMAND_GROOVE:
    cx->groove = &command->as.groove;
    cx->beat = 0;
    break;
  case VOICE_COMMAND_SECTION:
    if (++cx->n_sections >= cx->s_sections) {
      cx->s_sections *= 2;
      RESIZE(cx->sections, cx->s_sections);
    }
    cx->sections[cx->n_sections-1] = command_i;
    break;
  case VOICE_COMMAND_REPEAT:
    current_repeat = cx->sections[--cx->n_sections]+1;
    for (int repeat_i = floor(number_to_double(cx->base, command->as.repeat)); repeat_i > 0; --repeat_i)
      for (int r = current_repeat; r < command_i; ++r) {
	process_command(cx, out_file, instrument, voice, r);
      }
    break;
  case VOICE_COMMAND_CHORD:
    length = SAMPLE_RATE * 60 / cx->tempo;
    if (cx->groove && cx->groove->n_notes > 0)
      length *= number_to_double(1, cx->groove->notes[cx->beat % cx->groove->n_notes]);
    int pos = ftell(out_file);
    for (int n = 0; n < command->as.chord.n_notes; ++n) {
      double freq = cx->root * number_to_double(cx->base, command->as.chord.notes[n]);
      while (is_modulator(instrument, cx->osc % instrument->n_oscillators)) ++cx->osc;
      write_note(out_file, length, freq, instrument, cx->osc % instrument->n_oscillators);
      ++cx->osc;
      if ((n + 1) < command->as.chord.n_notes)
	fseek(out_file, pos, SEEK_SET);
    }
    ++cx->beat;
    break;
  case VOICE_COMMAND_NOTE:
    length = SAMPLE_RATE * 60 / cx->tempo;
    if (cx->groove && cx->groove->n_notes > 0)
      length *= number_to_double(1, cx->groove->notes[cx->beat++ % cx->groove->n_notes]);
    double freq = cx->root * number_to_double(cx->base, command->as.note);
    while (is_modulator(instrument, cx->osc % instrument->n_oscillators)) ++cx->osc;
    write_note(out_file, length, freq, instrument, cx->osc % instrument->n_oscillators);
    ++cx->osc;
    break;
  case VOICE_COMMAND_REST:
    length = SAMPLE_RATE * 60 / cx->tempo;
    if (cx->groove && cx->groove->n_notes > 0)
      length *= number_to_double(1, cx->groove->notes[cx->beat++ % cx->groove->n_notes]);
    for (int i = 0; i < length; ++i) {
      if (0 == fread(&sample, sizeof sample, 1, out_file)) sample = 0;
      else fseek(out_file, -sizeof sample, SEEK_CUR);
      fwrite(&sample, sizeof sample, 1, out_file);
    }
    break;
  }
}

int tunebook_write_book
(struct tunebook_book *book, struct tunebook_error *error) {
  int n_filename;
  char *filename;
  struct tunebook_song *song;
  struct tunebook_voice *voice;
  struct tunebook_instrument *instrument = NULL;
  FILE *out_file;
  struct tunebook_render_context cx;
  cx.s_sections = 8;
  cx.n_sections = 0;
  NEW(cx.sections, cx.s_sections);
  printf("book has %i %s to render\n", book->n_songs, book->n_songs == 1 ? "song" : "songs");
  for (int s = 0 ; s < book->n_songs; ++s) {
    song = &book->songs[s];
    printf("song %i: %s\n\tvoices: %i\n", s+1, song->name, song->n_voices);
    cx.base = 2;
    cx.root = 440;
    cx.tempo = 60;
    cx.groove = NULL;
    n_filename = 4 + strlen(song->name);
    filename = malloc(n_filename);
    strcpy(filename, song->name);
    strcpy(filename + n_filename - 4, ".l16");
    out_file = fopen(filename, "w+");
    for (int v = 0; v < song->n_voices; ++v) {
      cx.groove = NULL;
      cx.tempo = song->tempo;
      cx.root = song->root;
      cx.osc = 0;
      cx.beat = 0;
      voice = &song->voices[v];
      printf("\t- %s", voice->instrument);
      fseek(out_file, 0, SEEK_SET);
      for (int i = 0; i < book->n_instruments; ++i) {
	instrument = &book->instruments[i];
	if (!strcmp(voice->instrument, instrument->name)) break;
      }
      for (int c = 0; c < voice->n_commands; ++c) {
	int progress = 100 * ((double)c/voice->n_commands);
	if (progress % 10 == 0) {
	  putchar('.');
	  fflush(stdout);
	}
	process_command(&cx, out_file, instrument, voice, c);
      }
      putchar('\n');
    }
    fclose(out_file);
    free(filename);
  }
  return 0;
}

int main() {
  struct tunebook_book book;
  struct tunebook_error error;
  if (tunebook_read_file(stdin, &book, &error)) goto error;
  if (tunebook_write_book(&book, &error)) goto error;
  return 0;
 error:
  tunebook_print_error(error);
  return -1;
}
