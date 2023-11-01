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
    TOKEN_ADD,
    TOKEN_AM,
    TOKEN_ATTACK,
    TOKEN_BASE,
    TOKEN_CHORD_END,
    TOKEN_CHORD_START,
    TOKEN_CLIP,
    TOKEN_DECAY,
    TOKEN_DETUNE,
    TOKEN_ENV,
    TOKEN_FM,
    TOKEN_GROOVE,
    TOKEN_HZ,
    TOKEN_INCLUDE,
    TOKEN_INSTRUMENT,
    TOKEN_LEGATO,
    TOKEN_MODULATE,
    TOKEN_NOISE,
    TOKEN_NUMBER,
    TOKEN_PM,
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
    VOICE_COMMAND_BASE,
    VOICE_COMMAND_CHORD,
    VOICE_COMMAND_GROOVE,
    VOICE_COMMAND_LEGATO,
    VOICE_COMMAND_MODULATE,
    VOICE_COMMAND_NOTE,
    VOICE_COMMAND_REPEAT,
    VOICE_COMMAND_REST,
    VOICE_COMMAND_SECTION,
  } type;
  union {
    struct tunebook_number base, note, modulate, repeat, legato;
    struct tunebook_chord groove, chord;
  } as;
};

struct tunebook_error {
  enum {
    ERROR_EOF,
    ERROR_EXPECTED_CHORD_START,
    ERROR_EXPECTED_NUMBER,
    ERROR_EXPECTED_STRING,
    ERROR_FILE_NOT_FOUND,
    ERROR_UNIMPLEMENTED,
    ERROR_UNKNOWN_INSTRUMENT,
    ERROR_UNKNOWN_KEYWORD,
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
  else if (!strcmp(buffer, "include")) token->type = TOKEN_INCLUDE;
  else if (!strcmp(buffer, "legato")) token->type = TOKEN_LEGATO;
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

int tunebook_include_file
(FILE *in, struct tunebook_book *book, struct tunebook_error *error,
 int *s_instruments, int *s_songs) {
  FILE *included;
  struct tunebook_token token;
  int shape, i = 0, s_voices = 0, s_oscillators = 0, s_am_targets = 0,
    s_fm_targets = 0, s_pm_targets = 0, s_add_targets = 0,
    s_sub_targets = 0, s_env_targets = 0, s_commands = 0, s_notes = 0;
  struct tunebook_instrument *instrument = NULL;
  struct tunebook_oscillator *oscillator = NULL;
  struct tunebook_song *song = NULL;
  struct tunebook_voice *voice = NULL;
  struct tunebook_voice_command *command = NULL;
  for (;;) {
    if (tunebook_next_token(in, &token, error)) goto error;
    switch (token.type) {
    case TOKEN_INCLUDE:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_STRING) {
	error->type = ERROR_EXPECTED_STRING;
	goto error;
      }
      included = fopen(token.as.string, "r");
      if (!included) {
        error->type = ERROR_FILE_NOT_FOUND;
        goto error;
      }
      if (tunebook_include_file(included, book, error, s_instruments, s_songs))
        goto error;
      break;
    case TOKEN_INSTRUMENT:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_STRING) {
	error->type = ERROR_EXPECTED_STRING;
	goto error;
      }
      for (i = 0; i < book->n_instruments; ++i) {
        if (!strcmp(token.as.string, book->instruments[i].name)) break;
      }
      if (i == book->n_instruments) {
        if (++book->n_instruments >= *s_instruments) {
          *s_instruments *= 2;
          RESIZE(book->instruments, *s_instruments);
        }
        instrument = &book->instruments[book->n_instruments-1];
        s_oscillators = 4;
        instrument->name = token.as.string;
        instrument->n_oscillators = 0;
        NEW(instrument->oscillators, s_oscillators);
      } else {
        instrument = &book->instruments[i];
        s_oscillators = instrument->n_oscillators;
      }
      break;
    case TOKEN_BASE:
      if (++voice->n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(voice->commands, s_commands);
      }
      command = &voice->commands[voice->n_commands-1];
      command->type = VOICE_COMMAND_BASE;
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      command->as.base = token.as.number;
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
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_STRING) {
	error->type = ERROR_EXPECTED_STRING;
	goto error;
      }
      for (i = 0; i < instrument->n_oscillators; ++i) {
        if (!strcmp(token.as.string, instrument->oscillators[i].name)) break;
      }
      if (i == instrument->n_oscillators) {
        if (++instrument->n_oscillators >= s_oscillators) {
          s_oscillators *= 2;
          RESIZE(instrument->oscillators, s_oscillators);
        }
        oscillator = &instrument->oscillators[instrument->n_oscillators-1];
        s_am_targets = 2;
        s_fm_targets = 2;
        s_pm_targets = 2;
        s_add_targets = 2;
        s_sub_targets = 2;
        s_env_targets = 2;
        oscillator->name = token.as.string;
        oscillator->n_am_targets = 0;
        oscillator->n_fm_targets = 0;
        oscillator->n_pm_targets = 0;
        oscillator->n_add_targets = 0;
        oscillator->n_sub_targets = 0;
        oscillator->n_env_targets = 0;
        NEW(oscillator->am_targets, s_am_targets);
        NEW(oscillator->fm_targets, s_fm_targets);
        NEW(oscillator->pm_targets, s_pm_targets);
        NEW(oscillator->add_targets, s_add_targets);
        NEW(oscillator->sub_targets, s_sub_targets);
        NEW(oscillator->env_targets, s_env_targets);
        oscillator->shape = shape;
        oscillator->attack = 1.0/32.0;
        oscillator->clip = 0;
        oscillator->decay = 1.0/3.0;
        oscillator->sustain = 3.0/4.0;
        oscillator->release = 1.0/32.0;
        oscillator->volume = 1.0/2.0;
        oscillator->hz = 0;
        oscillator->detune = 1;
      } else {
        oscillator = &instrument->oscillators[i];
        oscillator->shape = shape;
        s_am_targets = oscillator->n_am_targets;
        s_fm_targets = oscillator->n_fm_targets;
        s_pm_targets = oscillator->n_pm_targets;
        s_add_targets = oscillator->n_add_targets;
        s_sub_targets = oscillator->n_sub_targets;
        s_env_targets = oscillator->n_env_targets;
      }
      break;
    case TOKEN_CLIP:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      oscillator->clip = number_to_double(1, token.as.number);
      break;
    case TOKEN_DETUNE:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      oscillator->detune = number_to_double(1, token.as.number);
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
	if (++oscillator->n_env_targets >= s_env_targets) {
	  s_env_targets *= 2;
	  RESIZE(oscillator->env_targets, s_env_targets);
	}
	oscillator->env_targets[oscillator->n_env_targets-1] = token.as.string;
      }
      break;
    case TOKEN_HZ:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      oscillator->hz = number_to_double(1, token.as.number);
      break;
    case TOKEN_ATTACK:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      oscillator->attack = number_to_double(1, token.as.number);
      break;
    case TOKEN_DECAY:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      oscillator->decay = number_to_double(1, token.as.number);
      break;
    case TOKEN_SUSTAIN:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      oscillator->sustain = number_to_double(1, token.as.number);
      break;
    case TOKEN_RELEASE:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      oscillator->release = number_to_double(1, token.as.number);
      break;
    case TOKEN_VOLUME:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      oscillator->volume = number_to_double(1, token.as.number);
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
	if (++oscillator->n_am_targets >= s_am_targets) {
	  s_am_targets *= 2;
	  RESIZE(oscillator->am_targets, s_am_targets);
	}
	oscillator->am_targets[oscillator->n_am_targets-1] = token.as.string;
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
	if (++oscillator->n_fm_targets >= s_fm_targets) {
	  s_fm_targets *= 2;
	  RESIZE(oscillator->fm_targets, s_fm_targets);
	}
	oscillator->fm_targets[oscillator->n_fm_targets-1] = token.as.string;
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
	if (++oscillator->n_pm_targets >= s_pm_targets) {
	  s_pm_targets *= 2;
	  RESIZE(oscillator->pm_targets, s_pm_targets);
	}
	oscillator->pm_targets[oscillator->n_pm_targets-1] = token.as.string;
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
	if (++oscillator->n_add_targets >= s_add_targets) {
	  s_add_targets *= 2;
	  RESIZE(oscillator->add_targets, s_add_targets);
	}
	oscillator->add_targets[oscillator->n_add_targets-1] = token.as.string;
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
	if (++oscillator->n_sub_targets >= s_sub_targets) {
	  s_sub_targets *= 2;
	  RESIZE(oscillator->sub_targets, s_sub_targets);
	}
	oscillator->sub_targets[oscillator->n_sub_targets-1] = token.as.string;
      }
      break;
    case TOKEN_SONG:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_STRING) {
	error->type = ERROR_EXPECTED_STRING;
	goto error;
      }
      for (i = 0; i < book->n_songs; ++i) {
        if (!strcmp(token.as.string, book->songs[i].name)) break;
      }
      if (i == book->n_songs) {
        if (++book->n_songs >= *s_songs) {
          *s_songs *= 2;
          RESIZE(book->songs, *s_songs);
        }
        song = &book->songs[book->n_songs-1];
        s_voices = 8;
        song->name = token.as.string;
        song->tempo = 60;
        song->root = 440;
        song->n_voices = 0;
        NEW(song->voices, s_voices);
      } else {
        song = &book->songs[i];
        s_voices = song->n_voices;
      }
      break;
    case TOKEN_TEMPO:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      song->tempo = number_to_double(1, token.as.number);
      break;
    case TOKEN_ROOT:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      song->root = number_to_double(1, token.as.number);
      break;
    case TOKEN_VOICE:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_STRING) {
	error->type = ERROR_EXPECTED_STRING;
	goto error;
      }
      if (++song->n_voices >= s_voices) {
	s_voices *= 2;
	RESIZE(song->voices, s_voices);
      }
      voice = &song->voices[song->n_voices-1];
      s_commands = 32;
      voice->instrument = token.as.string;
      voice->n_commands = 0;
      NEW(voice->commands, s_commands);
      break;
    case TOKEN_GROOVE:
      if (++voice->n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(voice->commands, s_commands);
      }
      command = &voice->commands[voice->n_commands-1];
      s_notes = 4;
      command->type = VOICE_COMMAND_GROOVE;
      command->as.groove.n_notes = 0;
      NEW(command->as.groove.notes, s_notes);
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
	if (++command->as.groove.n_notes >= s_notes) {
	  s_notes *= 2;
	  RESIZE(command->as.groove.notes, s_notes);
	}
	command->as.groove.notes[command->as.groove.n_notes-1] = token.as.number;
      }
      break;
    case TOKEN_CHORD_START:
      if (++voice->n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(voice->commands, s_commands);
      }
      command = &voice->commands[voice->n_commands-1];
      s_notes = 4;
      command->type = VOICE_COMMAND_CHORD;
      command->as.chord.n_notes = 0;
      NEW(command->as.chord.notes, s_notes);
      for (;;) {
	if (tunebook_next_token(in, &token, error)) goto error;
	if (token.type == TOKEN_CHORD_END) break;
	if (token.type != TOKEN_NUMBER) {
	  error->type = ERROR_EXPECTED_NUMBER;
	  goto error;
	}
	if (++command->as.chord.n_notes >= s_notes) {
	  s_notes *= 2;
	  RESIZE(command->as.chord.notes, s_notes);
	}
	command->as.chord.notes[command->as.chord.n_notes-1] = token.as.number;
      }
      break;
    case TOKEN_NUMBER:
      if (++voice->n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(voice->commands, s_commands);
      }
      command = &voice->commands[voice->n_commands-1];
      command->type = VOICE_COMMAND_NOTE;
      command->as.note = token.as.number;
      break;
    case TOKEN_SECTION:
      if (++voice->n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(voice->commands, s_commands);
      }
      command = &voice->commands[voice->n_commands-1];
      command->type = VOICE_COMMAND_SECTION;
      break;
    case TOKEN_REPEAT:
      if (++voice->n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(voice->commands, s_commands);
      }
      command = &voice->commands[voice->n_commands-1];
      command->type = VOICE_COMMAND_REPEAT;
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      command->as.repeat = token.as.number;
      break;
    case TOKEN_REST:
      if (++voice->n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(voice->commands, s_commands);
      }
      command = &voice->commands[voice->n_commands-1];
      command->type = VOICE_COMMAND_REST;
      break;
    case TOKEN_LEGATO:
      if (++voice->n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(voice->commands, s_commands);
      }
      command = &voice->commands[voice->n_commands-1];
      command->type = VOICE_COMMAND_LEGATO;
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      command->as.legato = token.as.number;
      break;
    case TOKEN_MODULATE:
      if (++voice->n_commands >= s_commands) {
	s_commands *= 2;
	RESIZE(voice->commands, s_commands);
      }
      command = &voice->commands[voice->n_commands-1];
      command->type = VOICE_COMMAND_MODULATE;
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      command->as.modulate = token.as.number;
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

int tunebook_read_file
(FILE *in, struct tunebook_book *book, struct tunebook_error *error) {
  int s_instruments = 8;
  int s_songs = 8;
  book->n_instruments = 0;
  book->n_songs = 0;
  NEW(book->instruments, s_instruments);
  NEW(book->songs, s_songs);
  return tunebook_include_file(in, book, error, &s_instruments, &s_songs);
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
  } else if (point < beat_length) {
    amp *= osc->sustain;
  } else {
    int release_end = beat_length * osc->release;
    if (release_end <= 0) {
      amp = 0;
    } else {
      double q = (double)(point - beat_length)/release_end;
      amp *= (1 - q) * osc->sustain;
    }
  }
  if (osc->clip > 0 && fabs(amp) > osc->clip) {
    amp = copysign(osc->clip, amp);
  }
  return amp;
}

void write_note
(FILE *out_file, int beat_length,
 double legato, double prev_freq, double targ_freq,
 struct tunebook_instrument *instrument, int osc_i) {
  SAMPLE sample;
  int length = beat_length * (1 + instrument->oscillators[osc_i].release);
  int legato_end = floor(beat_length * legato);
  double diff_freq = targ_freq - prev_freq;
  for (int i = 0; i < length; ++i) {
    if (0 == fread(&sample, sizeof sample, 1, out_file)) sample = 0;
    else fseek(out_file, -sizeof sample, SEEK_CUR);
    double freq;
    if (i >= legato_end || prev_freq == 0) freq = targ_freq;
    else {
      double p = ((float)i)/((float)legato_end);
      double t = 1 - pow(1 - p, 3);
      freq = prev_freq + (diff_freq * t);
    }
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
  double base, root, tempo, legato;
  struct tunebook_chord *groove;
  struct tunebook_voice_command *last_freq_command;
};

double previous_frequency(struct tunebook_render_context *cx, int chord_n) {
  // TODO handle base/modulate/other commands that change our basis
  if (!cx->last_freq_command) return 0;
  switch (cx->last_freq_command->type) {
  case VOICE_COMMAND_CHORD:
    if (chord_n >= cx->last_freq_command->as.chord.n_notes) return 0;
    return cx->root * number_to_double(cx->base, cx->last_freq_command->as.chord.notes[chord_n]);
  case VOICE_COMMAND_NOTE:
    return cx->root * number_to_double(cx->base, cx->last_freq_command->as.note);
  default:
    return 0;
  }
}

void process_command
(struct tunebook_render_context *cx,
 FILE *out_file,
 struct tunebook_instrument *instrument,
 struct tunebook_voice *voice,
 int command_i) {
  SAMPLE sample;
  int length, current_repeat;
  struct tunebook_voice_command *command = &voice->commands[command_i];
  int pos = ftell(out_file);
  switch (command->type) {
  case VOICE_COMMAND_BASE:
    cx->base = number_to_double(cx->base, command->as.base);
    break;
  case VOICE_COMMAND_LEGATO:
    cx->legato = number_to_double(cx->base, command->as.legato);
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
    for (int n = 0; n < command->as.chord.n_notes; ++n) {
      double prev_freq = previous_frequency(cx, n);
      double targ_freq = cx->root * number_to_double(cx->base, command->as.chord.notes[n]);
      while (is_modulator(instrument, cx->osc % instrument->n_oscillators)) ++cx->osc;
      write_note(out_file, length, cx->legato, prev_freq, targ_freq, instrument, cx->osc % instrument->n_oscillators);
      ++cx->osc;
      if ((n + 1) < command->as.chord.n_notes)
	fseek(out_file, pos, SEEK_SET);
    }
    ++cx->beat;
    cx->last_freq_command = command;
    break;
  case VOICE_COMMAND_NOTE:
    length = SAMPLE_RATE * 60 / cx->tempo;
    if (cx->groove && cx->groove->n_notes > 0)
      length *= number_to_double(1, cx->groove->notes[cx->beat++ % cx->groove->n_notes]);
    double prev_freq = previous_frequency(cx, 0);
    double targ_freq = cx->root * number_to_double(cx->base, command->as.note);
    while (is_modulator(instrument, cx->osc % instrument->n_oscillators)) ++cx->osc;
    write_note(out_file, length, cx->legato, prev_freq, targ_freq, instrument, cx->osc % instrument->n_oscillators);
    ++cx->osc;
    cx->last_freq_command = command;
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
    n_filename = 4 + strlen(song->name);
    filename = malloc(n_filename);
    strcpy(filename, song->name);
    strcpy(filename + n_filename - 4, ".l16");
    out_file = fopen(filename, "w+");
    for (int v = 0; v < song->n_voices; ++v) {
      cx.groove = NULL;
      cx.last_freq_command = NULL;
      cx.base = 2;
      cx.tempo = song->tempo;
      cx.root = song->root;
      cx.osc = 0;
      cx.beat = 0;
      cx.legato = 0;
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
