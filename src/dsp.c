/*
 * dsp.c: matemática determinística, osciladores, envelope, filtros,
 * pan, limitador e reverberação.
 */
#include "dsp.h"

#include <math.h>
#include <stdlib.h>

#define LN2   0.693147180559945309417232121458
#define LOG2E 1.442695040888963407359924681002
#define LOG2_10 3.321928094887362347870319429489

/* ---------------------------------------------------------------------------
 * Matemática
 * ------------------------------------------------------------------------ */

double dsp_sin2pi(double x)
{
    /* Reduz para r em [-0.5, 0.5) e usa a simetria sen(pi - t) = sen(t)
     * para ficar com o ângulo em [-pi/2, pi/2], onde a série converge rápido. */
    double r = x - floor(x + 0.5);
    if (r > 0.25) {
        r = 0.5 - r;
    } else if (r < -0.25) {
        r = -0.5 - r;
    }
    double t = 2.0 * DSP_PI * r;
    double t2 = t * t;
    /* Série de Taylor até t^21 (erro abaixo de 1e-16), avaliada por Horner. */
    double p = 1.0 / 51090942171709440000.0;  /* 1/21! */
    p = p * t2 - 1.0 / 121645100408832000.0;  /* 1/19! */
    p = p * t2 + 1.0 / 355687428096000.0;     /* 1/17! */
    p = p * t2 - 1.0 / 1307674368000.0;   /* 1/15! */
    p = p * t2 + 1.0 / 6227020800.0;      /* 1/13! */
    p = p * t2 - 1.0 / 39916800.0;        /* 1/11! */
    p = p * t2 + 1.0 / 362880.0;          /* 1/9!  */
    p = p * t2 - 1.0 / 5040.0;            /* 1/7!  */
    p = p * t2 + 1.0 / 120.0;             /* 1/5!  */
    p = p * t2 - 1.0 / 6.0;               /* 1/3!  */
    p = p * t2 + 1.0;
    return t * p;
}

double dsp_cos2pi(double x)
{
    return dsp_sin2pi(x + 0.25);
}

double dsp_exp2(double x)
{
    if (x > 1023.0) x = 1023.0;
    if (x < -1070.0) return 0.0;
    /* 2^x = 2^k * 2^f, com k inteiro e f em [-0.5, 0.5]. 2^k é exato (ldexp)
     * e 2^f = e^(f ln 2) sai de uma série curta. */
    double k = floor(x + 0.5);
    double y = (x - k) * LN2;
    double p = 1.0 / 479001600.0;         /* 1/12! */
    p = p * y + 1.0 / 39916800.0;
    p = p * y + 1.0 / 3628800.0;
    p = p * y + 1.0 / 362880.0;
    p = p * y + 1.0 / 40320.0;
    p = p * y + 1.0 / 5040.0;
    p = p * y + 1.0 / 720.0;
    p = p * y + 1.0 / 120.0;
    p = p * y + 1.0 / 24.0;
    p = p * y + 1.0 / 6.0;
    p = p * y + 0.5;
    p = p * y + 1.0;
    p = p * y + 1.0;
    return ldexp(p, (int)k);
}

double dsp_exp(double x)
{
    return dsp_exp2(x * LOG2E);
}

double dsp_tanh(double x)
{
    /* tanh(x) = 1 - 2 / (e^2x + 1). Escrita assim (e não como (e - 1)/(e + 1)),
     * cada operação cresce junto com x e o resultado nunca "volta" por erro de
     * arredondamento. A simetria tanh(-x) = -tanh(x) cuida dos negativos. */
    double ax = x < 0.0 ? -x : x;
    if (ax > 20.0) return x < 0.0 ? -1.0 : 1.0;
    double y = 1.0 - 2.0 / (dsp_exp(2.0 * ax) + 1.0);
    return x < 0.0 ? -y : y;
}

double note_frequency(int midi)
{
    return 440.0 * dsp_exp2((midi - 69) / 12.0);
}

/* ---------------------------------------------------------------------------
 * Osciladores
 * ------------------------------------------------------------------------ */

/*
 * PolyBLEP: um degrau de verdade (com banda limitada) tem uma "rampa" em volta
 * da descontinuidade. A correção soma a diferença entre essa rampa e o degrau
 * ingênuo, aproximada por um polinômio de grau 2 que só vale nas amostras
 * vizinhas ao salto (t a menos de dt dele).
 */
double poly_blep(double t, double dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

double osc_saw_naive(double phase)
{
    return 2.0 * phase - 1.0;
}

double osc_saw(double phase, double dt)
{
    /* O dente de serra cai de +1 para -1 quando a fase volta a 0. */
    return 2.0 * phase - 1.0 - poly_blep(phase, dt);
}

double osc_square_naive(double phase, double pw)
{
    return (phase < pw ? 1.0 : -1.0) - (2.0 * pw - 1.0);
}

double osc_square(double phase, double dt, double pw)
{
    /* Dois saltos por ciclo: sobe em fase 0 e desce em fase pw. */
    double v = phase < pw ? 1.0 : -1.0;
    v += poly_blep(phase, dt);
    double t2 = phase + 1.0 - pw;
    if (t2 >= 1.0) t2 -= 1.0;
    v -= poly_blep(t2, dt);
    return v - (2.0 * pw - 1.0); /* tira a média, que não é zero quando pw != 0.5 */
}

void tri_init(tri_osc *o)
{
    o->phase = 0.0;
    o->value = -1.0; /* a quadrada começa em +1: o triângulo sobe de -1 a +1 */
}

double tri_next(tri_osc *o, double dt)
{
    /* Integrar a quadrada dá um triângulo; integrar a versão com PolyBLEP dá um
     * triângulo com banda limitada. O pequeno vazamento impede que erros de
     * arredondamento se acumulem em notas longas. */
    double sq = osc_square(o->phase, dt, 0.5);
    o->value = o->value * (1.0 - 0.05 * dt) + 4.0 * dt * sq;
    o->phase += dt;
    if (o->phase >= 1.0) o->phase -= 1.0;
    return o->value;
}

double noise_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return (double)(x >> 8) / 8388608.0 - 1.0;
}

/* ---------------------------------------------------------------------------
 * Envelope
 * ------------------------------------------------------------------------ */

static double adsr_hold(const adsr *e, double t)
{
    if (e->a > 0.0 && t < e->a) return t / e->a;
    t -= e->a;
    if (e->d > 0.0 && t < e->d) return 1.0 - (1.0 - e->s) * (t / e->d);
    return e->s;
}

double adsr_level(const adsr *e, double t, double t_off)
{
    if (t < 0.0) return 0.0;
    if (t < t_off) return adsr_hold(e, t);
    /* O release começa do nível em que a nota estava ao ser solta, mesmo que
     * ainda no ataque: assim não há degrau (nem estalo). */
    double level = adsr_hold(e, t_off);
    double tr = t - t_off;
    if (e->r <= 0.0 || tr >= e->r) return 0.0;
    return level * (1.0 - tr / e->r);
}

/* ---------------------------------------------------------------------------
 * Filtros
 * ------------------------------------------------------------------------ */

void onepole_set(onepole *f, double cutoff)
{
    f->a = 1.0 - dsp_exp(-2.0 * DSP_PI * cutoff / SAMPLE_RATE);
}

double onepole_run(onepole *f, double x)
{
    f->y += f->a * (x - f->y);
    return f->y;
}

/* Coeficientes do "Audio EQ Cookbook" de Robert Bristow-Johnson. */
void biquad_set(biquad *f, biquad_type type, double cutoff, double q)
{
    double w = cutoff / SAMPLE_RATE;
    double cs = dsp_cos2pi(w), sn = dsp_sin2pi(w);
    double alpha = sn / (2.0 * q);
    double b0, b1, b2;
    switch (type) {
    case BQ_HIGHPASS:
        b0 = (1.0 + cs) / 2.0;
        b1 = -(1.0 + cs);
        b2 = (1.0 + cs) / 2.0;
        break;
    case BQ_BANDPASS:
        b0 = alpha;
        b1 = 0.0;
        b2 = -alpha;
        break;
    case BQ_LOWPASS:
    default:
        b0 = (1.0 - cs) / 2.0;
        b1 = 1.0 - cs;
        b2 = (1.0 - cs) / 2.0;
        break;
    }
    double a0 = 1.0 + alpha;
    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = -2.0 * cs / a0;
    f->a2 = (1.0 - alpha) / a0;
}

double biquad_run(biquad *f, double x)
{
    /* Forma direta transposta II: dois estados, boa precisão numérica. */
    double y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

double biquad_gain(const biquad *f, double freq)
{
    /* H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2), com z = e^(jw). */
    double w = freq / SAMPLE_RATE;
    double c1 = dsp_cos2pi(w), s1 = dsp_sin2pi(w);
    double c2 = dsp_cos2pi(2.0 * w), s2 = dsp_sin2pi(2.0 * w);
    double nr = f->b0 + f->b1 * c1 + f->b2 * c2, ni = -(f->b1 * s1 + f->b2 * s2);
    double dr = 1.0 + f->a1 * c1 + f->a2 * c2, di = -(f->a1 * s1 + f->a2 * s2);
    return sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}

/* ---------------------------------------------------------------------------
 * Mixagem
 * ------------------------------------------------------------------------ */

void pan_gains(double pan, double *left, double *right)
{
    if (pan < -1.0) pan = -1.0;
    if (pan > 1.0) pan = 1.0;
    /* Ângulo de 0 a pi/2: esquerda = cos, direita = sen. A soma dos quadrados é
     * sempre 1, então a potência não muda quando o som anda pelo estéreo. */
    double u = (pan + 1.0) / 8.0;
    *left = dsp_cos2pi(u);
    *right = dsp_sin2pi(u);
}

#define CLIP_KNEE 0.75

double soft_clip(double x)
{
    double ax = x < 0.0 ? -x : x;
    if (ax <= CLIP_KNEE) return x;
    double y = CLIP_KNEE + (1.0 - CLIP_KNEE) * dsp_tanh((ax - CLIP_KNEE) / (1.0 - CLIP_KNEE));
    return x < 0.0 ? -y : y;
}

int16_t to_int16(double x)
{
    double v = floor(x * 32767.0 + 0.5);
    if (!(v == v)) return 0; /* NaN */
    if (v > 32767.0) v = 32767.0;
    if (v < -32767.0) v = -32767.0;
    return (int16_t)v;
}

/* ---------------------------------------------------------------------------
 * Reverberação
 * ------------------------------------------------------------------------ */

/* Atrasos de Schroeder (1962), em ms. Números "primos entre si" evitam que os
 * ecos dos pentes coincidam e soem como uma nota metálica. */
static const double COMB_MS[4] = { 29.7, 37.1, 41.1, 43.7 };
static const double ALLPASS_MS[2] = { 5.0, 1.7 };
#define STEREO_SPREAD 23 /* amostras a mais no canal direito, como no Freeverb */

double comb_run(comb_filter *f, double x)
{
    double y = f->buf[f->pos];
    /* Passa-baixas na realimentação: os agudos somem mais rápido, como numa sala. */
    f->store = y * (1.0 - f->damp) + f->store * f->damp;
    f->buf[f->pos] = x + f->store * f->feedback;
    if (++f->pos == f->len) f->pos = 0;
    return y;
}

double allpass_run(allpass_filter *f, double x)
{
    /* y[n] = -g w[n] + w[n-D], com w[n] = x[n] + g w[n-D]: ganho 1 em todas as
     * frequências, só espalha a fase (deixa os ecos mais densos). */
    double d = f->buf[f->pos];
    double w = x + f->g * d;
    f->buf[f->pos] = w;
    if (++f->pos == f->len) f->pos = 0;
    return d - f->g * w;
}

int reverb_init(reverb *r, double rt60, double damp)
{
    int lens[2][6];
    size_t total = 0;
    for (int ch = 0; ch < 2; ch++) {
        for (int k = 0; k < 4; k++) {
            lens[ch][k] = (int)(COMB_MS[k] * SAMPLE_RATE / 1000.0 + 0.5) + ch * STEREO_SPREAD;
            total += (size_t)lens[ch][k];
        }
        for (int k = 0; k < 2; k++) {
            lens[ch][4 + k] = (int)(ALLPASS_MS[k] * SAMPLE_RATE / 1000.0 + 0.5) + ch * STEREO_SPREAD;
            total += (size_t)lens[ch][4 + k];
        }
    }
    r->memory = calloc(total, sizeof *r->memory);
    if (!r->memory) return -1;
    double *p = r->memory;
    for (int ch = 0; ch < 2; ch++) {
        for (int k = 0; k < 4; k++) {
            comb_filter *c = &r->comb[ch][k];
            c->buf = p;
            c->len = lens[ch][k];
            c->pos = 0;
            c->store = 0.0;
            c->damp = damp;
            /* Cada volta pelo pente dura len amostras; para cair 60 dB (fator
             * 1000) em rt60 segundos, o ganho por volta é 10^(-3 len / (rt60 fs)). */
            c->feedback = dsp_exp2(-3.0 * c->len / (rt60 * SAMPLE_RATE) * LOG2_10);
            p += c->len;
        }
        for (int k = 0; k < 2; k++) {
            allpass_filter *a = &r->ap[ch][k];
            a->buf = p;
            a->len = lens[ch][4 + k];
            a->pos = 0;
            a->g = 0.7;
            p += a->len;
        }
    }
    return 0;
}

void reverb_run(reverb *r, double in, double *left, double *right)
{
    double out[2];
    for (int ch = 0; ch < 2; ch++) {
        double sum = 0.0;
        for (int k = 0; k < 4; k++) sum += comb_run(&r->comb[ch][k], in);
        double y = sum * 0.25;
        y = allpass_run(&r->ap[ch][0], y);
        y = allpass_run(&r->ap[ch][1], y);
        out[ch] = y;
    }
    *left = out[0];
    *right = out[1];
}

void reverb_free(reverb *r)
{
    free(r->memory);
    r->memory = NULL;
}
