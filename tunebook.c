#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define SAMPLE_RATE 44100
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
  double attack, decay, sustain, release, volume, hz, detune;
  int n_am_targets, n_fm_targets, n_add_targets, n_sub_targets;
  char **am_targets, **fm_targets, **add_targets, **sub_targets;
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
    VOICE_COMMAND_NOTE,
    VOICE_COMMAND_MODULATE,
    VOICE_COMMAND_GROOVE,
    VOICE_COMMAND_CHORD,
    VOICE_COMMAND_SECTION,
    VOICE_COMMAND_REPEAT,
    VOICE_COMMAND_REST,
  } type;
  union {
    struct tunebook_number base, note, modulate, repeat;
    struct tunebook_chord groove, chord;
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
      OSCILLATOR.attack = 1.0/32.0;
      OSCILLATOR.decay = 1.0/3.0;
      OSCILLATOR.sustain = 3.0/4.0;
      OSCILLATOR.release = 1.0/32.0;
      OSCILLATOR.volume = 1.0/2.0;
      OSCILLATOR.hz = 0;
      OSCILLATOR.detune = 1;
      break;
    case TOKEN_DETUNE:
      if (tunebook_next_token(in, &token, error)) goto error;
      if (token.type != TOKEN_NUMBER) {
	error->type = ERROR_EXPECTED_NUMBER;
	goto error;
      }
      OSCILLATOR.detune = number_to_double(1, token.as.number);
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
 error:
  // TODO cleanup excess memory
  if (error->type == ERROR_EOF) return 0;
  return -1;
}

int is_modulator(struct tunebook_instrument *instrument, int o) {
  struct tunebook_oscillator *osc = &instrument->oscillators[o];
  return osc->n_am_targets
    + osc->n_fm_targets
    + osc->n_add_targets
    + osc->n_sub_targets;
}

double amp_at_point
(int point, int beat_length, double freq,
 struct tunebook_instrument *instrument,
 int o) {
  osc_fun wave_func;
  struct tunebook_oscillator *osc = &instrument->oscillators[o];
  int attack = osc->attack * beat_length;
  int decay = attack + (osc->decay * (double)beat_length);
  int release = beat_length;
  int length = beat_length * (1 + osc->release);
  switch (osc->shape) {
  case OSC_SAW: wave_func = saw; break;
  case OSC_TRIANGLE: wave_func = triangle; break;
  case OSC_SQUARE: wave_func = square; break;
  case OSC_SINE: wave_func = sin; break;
  }
  double fm = 0, am = 0, add = 0, sub = 0;
  for (int i = 0; i < instrument->n_oscillators; ++i) {
    if (i == o) continue;
    for (int j = 0; j < instrument->oscillators[i].n_am_targets; ++j) {
      if (strcmp(osc->name, instrument->oscillators[i].am_targets[j])) continue;
      am += amp_at_point(point, beat_length, freq, instrument, i);
    }
    for (int j = 0; j < instrument->oscillators[i].n_fm_targets; ++j) {
      if (strcmp(osc->name, instrument->oscillators[i].fm_targets[j])) continue;
      fm += amp_at_point(point, beat_length, freq, instrument, i);
    }
    for (int j = 0; j < instrument->oscillators[i].n_add_targets; ++j) {
      if (strcmp(osc->name, instrument->oscillators[i].add_targets[j])) continue;
      add += amp_at_point(point, beat_length, freq, instrument, i);
    }
    for (int j = 0; j < instrument->oscillators[i].n_sub_targets; ++j) {
      if (strcmp(osc->name, instrument->oscillators[i].sub_targets[j])) continue;
      sub += amp_at_point(point, beat_length, freq, instrument, i);
    }
  }
  freq *= osc->detune;
  if (osc->hz) freq = osc->hz;
  double amp = (1 + am) * osc->volume * wave_func(point * (freq * 1 + fm) * 2 * M_PI / SAMPLE_RATE);
  amp += add;
  amp -= sub;
  if (point < attack) {
    amp *= (double)point/attack;
  } else if (point < decay) {
    double q = (double)(point-attack)/(decay-attack);
    amp *= 1 - (q * (1 - osc->sustain));
  } else if (point < release) {;
    amp *= osc->sustain;
  } else {
    double q = (double)(point-release)/(length-release);
    amp *= (1 - q) * osc->sustain;
  };
  if (amp > 1) return 1;
  if (amp < -1) return -1;
  return amp;
}

void write_note
(FILE *out_file, int beat_length, double freq,
 struct tunebook_instrument *instrument,
 int osc) {
  int16_t sample;
  int length = beat_length * (1 + instrument->oscillators[osc].release);
  for (int i = 0; i < length; ++i) {
    if (0 == fread(&sample, sizeof sample, 1, out_file)) sample = 0;
    else fseek(out_file, -sizeof sample, SEEK_CUR);
    sample += INT16_MAX * amp_at_point(i, beat_length, freq, instrument, osc);
    fwrite(&sample, sizeof sample, 1, out_file);
  }
  fseek(out_file, (sizeof sample) * (beat_length-length), SEEK_CUR);
}

int tunebook_write_book
(struct tunebook_book *book, struct tunebook_error *error) {
  int16_t sample;
  int n_filename, beat, length, o;
  char *filename;
  struct tunebook_song *song;
  struct tunebook_voice *voice;
  struct tunebook_instrument *instrument;
  struct tunebook_voice_command *command;
  struct tunebook_chord *groove;
  double tempo = 60, root = 440, base = 2;
  FILE *out_file;
  osc_fun f;
  for (int s = 0 ; s < book->n_songs; ++s) {
    song = &book->songs[s];
    n_filename = 4 + strlen(song->name);
    filename = malloc(n_filename);
    strcpy(filename, song->name);
    strcpy(filename + n_filename - 4, ".l16");
    out_file = fopen(filename, "w+");
    for (int v = 0; v < song->n_voices; ++v) {
      groove = NULL;
      tempo = song->tempo;
      root = song->root;
      voice = &song->voices[v];
      fseek(out_file, 0, SEEK_SET);
      o = beat = 0;
      for (int i = 0; i < book->n_instruments; ++i) {
	instrument = &book->instruments[i];
	if (!strcmp(voice->instrument, instrument->name)) break;
      }
      for (int c = 0; c < voice->n_commands; ++c) {
	command = &voice->commands[c];
	switch (command->type) {
	case VOICE_COMMAND_BASE:
	  base = number_to_double(base, command->as.base);
	  break;
	case VOICE_COMMAND_MODULATE:
	  root *= number_to_double(base, command->as.modulate);
	  break;
	case VOICE_COMMAND_GROOVE:
	  groove = &command->as.groove;
	  break;
	case VOICE_COMMAND_SECTION:
	case VOICE_COMMAND_REPEAT:
	  error->type = ERROR_UNIMPLEMENTED;
	  return -1;
	case VOICE_COMMAND_CHORD:
	  length = SAMPLE_RATE * 60 / tempo;
	  if (groove && groove->n_notes > 0)
	    length *= number_to_double(1, groove->notes[beat % groove->n_notes]);
	  for (int n = 0; n < command->as.chord.n_notes; ++n) {
	    double freq = root * number_to_double(base, command->as.chord.notes[n]);
	    int pos = ftell(out_file);
	    while (is_modulator(instrument, o)) o = (o + 1) % instrument->n_oscillators;
	    write_note(out_file, length, freq, instrument, o++);
	    if (n + 1 < command->as.chord.n_notes)
	      fseek(out_file, pos, SEEK_SET);
	  }
	  ++beat;
	  break;
	case VOICE_COMMAND_NOTE:
	  length = SAMPLE_RATE * 60 / tempo;
	  if (groove && groove->n_notes > 0)
	    length *= number_to_double(1, groove->notes[beat++ % groove->n_notes]);
	  double freq = root * number_to_double(base, command->as.note);
	  while (is_modulator(instrument, o)) o = (o + 1) % instrument->n_oscillators;
	  write_note(out_file, length, freq, instrument, o++);
	  break;
	case VOICE_COMMAND_REST:
	  length = SAMPLE_RATE * 60 / tempo;
	  if (groove && groove->n_notes > 0)
	    length *= number_to_double(1, groove->notes[beat++ % groove->n_notes]);
	  for (int i = 0; i < length; ++i) {
	    if (0 == fread(&sample, sizeof sample, 1, out_file)) sample = 0;
	    else fseek(out_file, -sizeof sample, SEEK_CUR);
	    fwrite(&sample, sizeof sample, 1, out_file);
	  }
	  break;
	}
      }
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
