/*
 * render.h: transforma as notas da partitura em amostras estéreo de 16 bits.
 *
 * A síntese é feita em blocos: a cada bloco entram as vozes que começam nele,
 * todas as vozes ativas somam o seu som e as que terminaram saem. Assim a
 * memória usada não depende do tamanho da música.
 */
#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dsp.h"
#include "instrumentos.h"
#include "partitura.h"

typedef struct {
    long start, off;  /* em amostras */
    int  event;       /* índice em song.events */
} scheduled_note;

typedef struct {
    const song     *s;
    scheduled_note *order;     /* notas em ordem de início */
    int             nnotes, next;
    voice          *voices;    /* vozes tocando agora */
    int             nvoices, capvoices;
    reverb          rev;
    bool            use_reverb;
    long            total;     /* duração em amostras (quadros estéreo) */
    long            pos;
    double          peak;      /* maior amplitude antes do limitador */
} renderer;

/* Converte ticks em amostras com aritmética inteira (sem acumular erro). */
long ticks_to_samples(const song *s, long ticks);

int    renderer_init(renderer *r, const song *s, bool use_reverb);
/* Gera até "frames" quadros (esquerda e direita intercalados). Retorna quantos gerou. */
size_t renderer_run(renderer *r, int16_t *out, size_t frames);
void   renderer_free(renderer *r);

#endif
