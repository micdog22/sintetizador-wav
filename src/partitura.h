/*
 * partitura.h: lê a partitura em texto e devolve as notas de cada faixa.
 *
 * O tempo é contado em "ticks": 768 por semibreve (192 por semínima). Com
 * esse número, toda duração aceita (de 1/64 a 64, com até dois pontos e
 * quiálteras de 3) vira um inteiro exato, sem erro de arredondamento.
 */
#ifndef PARTITURA_H
#define PARTITURA_H

#include <stddef.h>

#define TICKS_WHOLE   768
#define TICKS_QUARTER 192

typedef enum {
    INSTR_PIANO,
    INSTR_ORGAO,
    INSTR_BAIXO,
    INSTR_FLAUTA,
    INSTR_CHIPTUNE,
    INSTR_SINO,
    INSTR_BATERIA,
    INSTR_COUNT
} instrument_id;

typedef enum {
    DRUM_BUMBO,
    DRUM_CAIXA,
    DRUM_CHIMBAL,
    DRUM_CHIMBAL_ABERTO,
    DRUM_PRATO,
    DRUM_COUNT
} drum_id;

typedef struct {
    int    track;
    long   start;     /* ticks desde o início */
    long   dur;       /* ticks */
    int    key;       /* nota MIDI de 0 a 127, ou drum_id na bateria */
    double velocity;  /* 0 a 1, vem da dinâmica (pp a ff) */
} note_event;

typedef struct {
    char          name[48];
    instrument_id instrument;
    int           volume;    /* 0 a 100 */
    int           pan;       /* -100 (esquerda) a 100 (direita) */
    int           reverb;    /* 0 a 100: quanto do som vai para a reverberação */
    long          length;    /* ticks */
    int           nnotes;
} track_info;

typedef struct {
    int         tempo;       /* semínimas por minuto */
    int         beats, unit; /* fórmula de compasso, como 3/4 */
    track_info *tracks;
    int         ntracks;
    note_event *events;
    int         nevents;
} song;

/* Lê a partitura. Em caso de erro, devolve -1 e escreve em err uma mensagem
 * no formato "arquivo:linha:coluna: erro: ...". */
int  song_parse(const char *src, const char *file, song *s, char *err, size_t errcap);
void song_free(song *s);

long        song_length(const song *s);              /* ticks */
double      song_seconds(const song *s, long ticks); /* ticks para segundos */
const char *instrument_name(instrument_id id);
const char *drum_name(drum_id id);

/* Funções internas expostas para os testes. */
int parse_pitch_word(const char *word, int *midi);  /* "C#4", "sib3", "ré5"... 0 ou -1 */
int parse_duration_text(const char *text, long *ticks); /* "1/8.", "1/4t"... 0 ou -1 */

#endif
