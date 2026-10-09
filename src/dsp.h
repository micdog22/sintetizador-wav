/*
 * dsp.h: os blocos de processamento de sinal do sintetizador.
 *
 * Tudo aqui usa só soma, subtração, multiplicação, divisão, floor e ldexp,
 * que dão o mesmo resultado bit a bit em qualquer máquina IEEE 754. Por isso
 * seno e exponencial são calculados por polinômios próprios em vez da libm:
 * a mesma partitura gera exatamente o mesmo WAV no macOS e no Linux.
 */
#ifndef DSP_H
#define DSP_H

#include <stdint.h>

#define SAMPLE_RATE 44100
#define DSP_PI 3.14159265358979323846

/* ---- Matemática determinística ---- */
double dsp_sin2pi(double x);   /* sen(2 pi x) */
double dsp_cos2pi(double x);   /* cos(2 pi x) */
double dsp_exp2(double x);     /* 2 elevado a x */
double dsp_exp(double x);      /* e elevado a x */
double dsp_tanh(double x);

/* Frequência da nota MIDI n em temperamento igual: 440 * 2^((n - 69) / 12). */
double note_frequency(int midi);

/* ---- Osciladores ----
 * A fase vai de 0 a 1 e avança dt = f / taxa por amostra. */
double poly_blep(double t, double dt);
double osc_saw(double phase, double dt);              /* dente de serra com PolyBLEP */
double osc_square(double phase, double dt, double pw);/* pulso com largura pw, sem nível DC */
double osc_saw_naive(double phase);
double osc_square_naive(double phase, double pw);

/* Triângulo: integra a onda quadrada com PolyBLEP. Precisa de estado. */
typedef struct {
    double phase;
    double value;
} tri_osc;
void   tri_init(tri_osc *o);
double tri_next(tri_osc *o, double dt);

/* Ruído branco uniforme em [-1, 1) com xorshift32. */
double noise_next(uint32_t *state);

/* ---- Envelope ADSR (segmentos lineares) ----
 * a, d e r em segundos, s é o nível de sustentação (0 a 1). */
typedef struct {
    double a, d, s, r;
} adsr;

/* Nível no instante t (segundos desde o início da nota), com a nota solta em t_off. */
double adsr_level(const adsr *e, double t, double t_off);

/* ---- Filtros ---- */
typedef struct {
    double a, y;
} onepole;
void   onepole_set(onepole *f, double cutoff);
double onepole_run(onepole *f, double x);

typedef enum { BQ_LOWPASS, BQ_HIGHPASS, BQ_BANDPASS } biquad_type;
typedef struct {
    double b0, b1, b2, a1, a2;
    double z1, z2;
} biquad;
void   biquad_set(biquad *f, biquad_type type, double cutoff, double q);
double biquad_run(biquad *f, double x);
/* Ganho |H| na frequência freq, calculado pelos coeficientes (para testes). */
double biquad_gain(const biquad *f, double freq);

/* ---- Mixagem ---- */
/* Pan de potência constante: pan de -1 (esquerda) a 1 (direita). */
void    pan_gains(double pan, double *left, double *right);
/* Limitador suave: linear até 0.75 e depois curva tanh que nunca passa de 1. */
double  soft_clip(double x);
int16_t to_int16(double x);

/* ---- Reverberação de Schroeder: 4 filtros pente em paralelo e 2 passa-tudo em série ---- */
typedef struct {
    double *buf;
    int     len, pos;
    double  feedback, damp, store;
} comb_filter;

typedef struct {
    double *buf;
    int     len, pos;
    double  g;
} allpass_filter;

typedef struct {
    comb_filter    comb[2][4];
    allpass_filter ap[2][2];
    double        *memory;
} reverb;

/* rt60: tempo (s) para o som cair 60 dB; damp: 0 a 1, quanto os agudos morrem antes. */
int    reverb_init(reverb *r, double rt60, double damp);
void   reverb_run(reverb *r, double in, double *left, double *right);
void   reverb_free(reverb *r);
double allpass_run(allpass_filter *f, double x);
double comb_run(comb_filter *f, double x);

#endif
