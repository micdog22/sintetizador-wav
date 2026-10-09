/*
 * instrumentos.h: cada nota tocando vira uma "voz", com o seu próprio
 * oscilador, envelope e filtros.
 */
#ifndef INSTRUMENTOS_H
#define INSTRUMENTOS_H

#include <stdint.h>

#include "dsp.h"
#include "partitura.h"

typedef struct {
    instrument_id instrument;
    int     key;          /* nota MIDI ou tambor */
    double  freq;
    double  amp;          /* velocidade x volume da faixa x ganho do instrumento */
    double  gain_l, gain_r, send;
    long    start, off, end; /* em amostras: começo, nota solta e fim do som */
    long    n;               /* amostras já geradas */

    /* Estado dos osciladores e filtros. */
    double   phase[6];        /* fases dos osciladores, de 0 a 1 */
    double   level1, k1;      /* decaimentos exponenciais: level *= k a cada amostra */
    double   level2, k2;
    tri_osc  tri;
    uint32_t rng;
    biquad   bq;
} voice;

/* Prepara a voz de uma nota. start e off são instantes em amostras;
 * pan de -1 a 1, volume e send de 0 a 1. */
void voice_init(voice *v, instrument_id instrument, int key, double velocity,
                long start, long off, double volume, double pan, double send);

/* Gera count amostras (a partir de onde parou) e soma em left, right e send. */
void voice_render(voice *v, double *left, double *right, double *send, int count);

/* Quanto o som continua depois de a nota ser solta (release ou cauda do tambor). */
long instrument_tail(instrument_id instrument, int key);

/* Envelope ADSR de cada instrumento (exposto para os testes). */
const adsr *instrument_envelope(instrument_id instrument);

#endif
