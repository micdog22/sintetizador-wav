/*
 * instrumentos.c: a receita de som de cada instrumento.
 *
 * Todos seguem o mesmo esquema: osciladores geram a forma de onda, filtros
 * moldam o timbre e o envelope ADSR controla o volume ao longo da nota.
 * Decaimentos exponenciais (brilho do piano, corpo do sino, tambores) são
 * feitos multiplicando um nível por uma constante a cada amostra.
 */
#include "instrumentos.h"

#include <string.h>

static const adsr ENVELOPES[INSTR_COUNT] = {
    [INSTR_PIANO]    = { 0.003, 0.00, 1.00, 0.30 },
    [INSTR_ORGAO]    = { 0.008, 0.05, 0.90, 0.06 },
    [INSTR_BAIXO]    = { 0.004, 0.20, 0.70, 0.07 },
    [INSTR_FLAUTA]   = { 0.070, 0.10, 0.85, 0.12 },
    [INSTR_CHIPTUNE] = { 0.002, 0.06, 0.65, 0.05 },
    [INSTR_SINO]     = { 0.001, 0.00, 1.00, 0.90 },
    [INSTR_BATERIA]  = { 0.000, 0.00, 1.00, 0.00 },
};

/* Ganho de cada instrumento, medido para que todos tenham volume (RMS) parecido. */
static const double INSTR_GAIN[INSTR_COUNT] = {
    [INSTR_PIANO] = 0.56, [INSTR_ORGAO] = 0.66, [INSTR_BAIXO] = 0.55, [INSTR_FLAUTA] = 0.50,
    [INSTR_CHIPTUNE] = 0.40, [INSTR_SINO] = 0.50, [INSTR_BATERIA] = 1.00,
};

/* Tambores: duração (s) e ganho. */
static const double DRUM_LEN[DRUM_COUNT] = { 0.50, 0.30, 0.12, 0.50, 1.80 };
static const double DRUM_GAIN[DRUM_COUNT] = { 0.95, 0.60, 0.32, 0.30, 0.28 };

/* Frequências dos osciladores de pulso do chimbal da TR-808 (Hz). */
static const double METAL_HZ[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

/* Fator por amostra para um decaimento com constante de tempo tau (s). */
static double decay_factor(double tau)
{
    return dsp_exp(-1.0 / (tau * SAMPLE_RATE));
}

const adsr *instrument_envelope(instrument_id instrument)
{
    return &ENVELOPES[instrument];
}

long instrument_tail(instrument_id instrument, int key)
{
    if (instrument == INSTR_BATERIA) return (long)(DRUM_LEN[key] * SAMPLE_RATE);
    return (long)(ENVELOPES[instrument].r * SAMPLE_RATE) + 1;
}

void voice_init(voice *v, instrument_id instrument, int key, double velocity,
                long start, long off, double volume, double pan, double send)
{
    memset(v, 0, sizeof *v);
    v->instrument = instrument;
    v->key = key;
    v->freq = instrument == INSTR_BATERIA ? 0.0 : note_frequency(key);
    v->amp = velocity * volume * INSTR_GAIN[instrument];
    pan_gains(pan, &v->gain_l, &v->gain_r);
    v->send = send;
    v->start = start;
    v->off = off;
    v->end = (instrument == INSTR_BATERIA ? start : off) + instrument_tail(instrument, key);
    /* Semente do ruído derivada da nota: o resultado é sempre o mesmo. */
    v->rng = 0x9E3779B9u ^ ((uint32_t)key * 2654435761u) ^ (uint32_t)start;
    if (v->rng == 0) v->rng = 1;
    tri_init(&v->tri);

    /* Constantes de tempo (s) dos dois decaimentos de cada instrumento. */
    double tau1 = 0.0, tau2 = 0.0;
    switch (instrument) {
    case INSTR_PIANO: tau1 = 1.6; tau2 = 0.35; break;  /* corpo e brilho */
    case INSTR_BAIXO: tau2 = 0.08; break;              /* abertura do filtro */
    case INSTR_FLAUTA: tau2 = 0.08; break;             /* sopro do ataque */
    case INSTR_SINO: tau1 = 1.8; tau2 = 0.8; break;    /* corpo e brilho */
    case INSTR_BATERIA: {
        static const double TAU[DRUM_COUNT] = { 0.22, 0.12, 0.03, 0.16, 0.60 };
        tau1 = TAU[key];
        tau2 = key == DRUM_BUMBO ? 0.04 : 0.05;        /* queda de altura ou do tom */
        break;
    }
    default: break;
    }
    v->level1 = v->level2 = 1.0;
    v->k1 = tau1 > 0.0 ? decay_factor(tau1) : 1.0;
    v->k2 = tau2 > 0.0 ? decay_factor(tau2) : 1.0;

    if (instrument == INSTR_FLAUTA) {
        double center = v->freq * 2.0 < 6000.0 ? v->freq * 2.0 : 6000.0;
        biquad_set(&v->bq, BQ_BANDPASS, center, 1.5); /* sopro em volta do 2o harmônico */
    } else if (instrument == INSTR_BATERIA) {
        if (key == DRUM_CAIXA) biquad_set(&v->bq, BQ_HIGHPASS, 1800.0, 0.8);
        if (key == DRUM_CHIMBAL || key == DRUM_CHIMBAL_ABERTO) biquad_set(&v->bq, BQ_HIGHPASS, 7000.0, 0.9);
        if (key == DRUM_PRATO) biquad_set(&v->bq, BQ_HIGHPASS, 5000.0, 0.7);
    }
}

/* Avança os dois decaimentos exponenciais em uma amostra. */
static void decay_step(voice *v)
{
    v->level1 *= v->k1;
    v->level2 *= v->k2;
}

/* Envelope do instrumento no instante da amostra n da voz. */
static double env_at(const voice *v, long n)
{
    double t = (double)n / SAMPLE_RATE;
    double t_off = (double)(v->off - v->start) / SAMPLE_RATE;
    return adsr_level(&ENVELOPES[v->instrument], t, t_off);
}

static void advance_phase(double *phase, double dt)
{
    *phase += dt;
    if (*phase >= 1.0) *phase -= 1.0;
}

/* Piano elétrico por FM: uma senoide modula a fase de outra de mesma
 * frequência. O índice de modulação cai com o tempo, então o som começa
 * brilhante e fica mais puro, como num piano de verdade. */
static double piano(voice *v, double dt)
{
    double body = v->level1, bright = v->level2;
    double index = 0.25 + 1.6 * bright;
    double fm = dsp_sin2pi(v->phase[0] + index * dsp_sin2pi(v->phase[0]) / (2.0 * DSP_PI));
    double x = body * (0.85 * fm + 0.15 * bright * dsp_sin2pi(2.0 * v->phase[0]));
    advance_phase(&v->phase[0], dt);
    decay_step(v);
    return x;
}

/* Órgão de "drawbars": soma de harmônicos com volumes fixos e um vibrato leve. */
static double organ(voice *v, double dt)
{
    static const double H[5] = { 1.0, 2.0, 3.0, 4.0, 6.0 };
    static const double A[5] = { 1.0, 0.55, 0.30, 0.25, 0.12 };
    double x = 0.0;
    for (int k = 0; k < 5; k++) {
        if (H[k] * dt < 0.45) x += A[k] * dsp_sin2pi(H[k] * v->phase[0]); /* sem passar de Nyquist */
    }
    double vib = dsp_sin2pi(v->phase[1]);
    advance_phase(&v->phase[0], dt * (1.0 + 0.002 * vib));
    advance_phase(&v->phase[1], 6.5 / SAMPLE_RATE);
    return x / 2.22;
}

/* Baixo subtrativo: dente de serra mais uma quadrada uma oitava abaixo,
 * por um passa-baixas ressonante que fecha logo depois do ataque. */
static double bass(voice *v, long n, double dt)
{
    /* Recalcular o filtro custa um seno e um cosseno: a cada 16 amostras basta. */
    if (n % 16 == 0) biquad_set(&v->bq, BQ_LOWPASS, 160.0 + 2400.0 * v->level2, 1.3);
    double raw = 0.7 * osc_saw(v->phase[0], dt) + 0.4 * osc_square(v->phase[1], dt * 0.5, 0.5);
    advance_phase(&v->phase[0], dt);
    advance_phase(&v->phase[1], dt * 0.5);
    decay_step(v);
    return biquad_run(&v->bq, raw);
}

/* Flauta: senoide com um pouco de triângulo, vibrato que entra devagar e
 * ruído de sopro filtrado, mais forte no começo da nota. */
static double flute(voice *v, long n, double dt)
{
    double t = (double)n / SAMPLE_RATE;
    double ramp = (t - 0.25) / 0.4;
    ramp = ramp < 0.0 ? 0.0 : ramp > 1.0 ? 1.0 : ramp;
    double vdt = dt * (1.0 + 0.006 * ramp * dsp_sin2pi(v->phase[1]));
    double tone = 0.8 * dsp_sin2pi(v->phase[0]) + 0.2 * tri_next(&v->tri, vdt);
    double breath = biquad_run(&v->bq, noise_next(&v->rng)) * (0.05 + 0.25 * v->level2);
    advance_phase(&v->phase[0], vdt);
    advance_phase(&v->phase[1], 5.2 / SAMPLE_RATE);
    decay_step(v);
    return tone + breath;
}

/* Chiptune: onda de pulso de 25%, a mais famosa dos videogames de 8 bits. */
static double chip(voice *v, long n, double dt)
{
    double t = (double)n / SAMPLE_RATE;
    double depth = t > 0.3 ? 0.004 : 0.0;
    double x = osc_square(v->phase[0], dt, 0.25);
    advance_phase(&v->phase[0], dt * (1.0 + depth * dsp_sin2pi(v->phase[1])));
    advance_phase(&v->phase[1], 6.0 / SAMPLE_RATE);
    return x;
}

/* Sino por FM com razão 1,4 entre modulador e portadora: os parciais não são
 * harmônicos, e é isso que soa como metal. */
static double bell(voice *v, double dt)
{
    double index = 3.5 * v->level2;
    double fm = dsp_sin2pi(v->phase[0] + index * dsp_sin2pi(v->phase[1]) / (2.0 * DSP_PI));
    double x = v->level1 * (0.7 * fm + 0.3 * dsp_sin2pi(v->phase[0]));
    advance_phase(&v->phase[0], dt);
    advance_phase(&v->phase[1], dt * 1.4);
    decay_step(v);
    return x;
}

/* Seis ondas quadradas desafinadas entre si: o "metal" dos pratos da 808. */
static double metal(voice *v, double scale)
{
    double x = 0.0;
    for (int k = 0; k < 6; k++) {
        x += osc_square_naive(v->phase[k], 0.5);
        advance_phase(&v->phase[k], METAL_HZ[k] * scale / SAMPLE_RATE);
    }
    return x / 6.0;
}

static double drum(voice *v, long n)
{
    double t = (double)n / SAMPLE_RATE;
    double x = 0.0;
    double body = v->level1, fast = v->level2;
    switch (v->key) {
    case DRUM_BUMBO: {
        /* Senoide cuja frequência despenca de 168 Hz para 48 Hz: o "tum". */
        double f = 48.0 + 120.0 * fast;
        double attack = t < 0.002 ? t / 0.002 : 1.0;
        x = attack * body * dsp_sin2pi(v->phase[0]);
        if (t < 0.003) x += 0.35 * noise_next(&v->rng) * (1.0 - t / 0.003); /* estalo do ataque */
        advance_phase(&v->phase[0], f / SAMPLE_RATE);
        break;
    }
    case DRUM_CAIXA: {
        /* Um tom curto (a pele) mais ruído agudo (a esteira). */
        double tone = 0.5 * fast * dsp_sin2pi(v->phase[0]) + 0.25 * fast * dsp_sin2pi(v->phase[1]);
        double snares = biquad_run(&v->bq, noise_next(&v->rng));
        x = tone + 0.8 * body * snares;
        advance_phase(&v->phase[0], 185.0 / SAMPLE_RATE);
        advance_phase(&v->phase[1], 330.0 / SAMPLE_RATE);
        break;
    }
    case DRUM_CHIMBAL:
    case DRUM_CHIMBAL_ABERTO:
        x = body * biquad_run(&v->bq, metal(v, 1.7) + 0.5 * noise_next(&v->rng));
        break;
    case DRUM_PRATO:
    default:
        x = body * biquad_run(&v->bq, metal(v, 1.3) + 0.7 * noise_next(&v->rng));
        break;
    }
    decay_step(v);
    /* Rampa de 5 ms no fim, para o corte não estalar. */
    long len = v->end - v->start, ramp = SAMPLE_RATE / 200;
    if (len - n < ramp) x *= (double)(len - n) / ramp;
    return x * DRUM_GAIN[v->key];
}

void voice_render(voice *v, double *left, double *right, double *send, int count)
{
    double dt = v->freq / SAMPLE_RATE;
    for (int i = 0; i < count; i++) {
        long n = v->n++;
        double x;
        switch (v->instrument) {
        case INSTR_PIANO:    x = piano(v, dt); break;
        case INSTR_ORGAO:    x = organ(v, dt); break;
        case INSTR_BAIXO:    x = bass(v, n, dt); break;
        case INSTR_FLAUTA:   x = flute(v, n, dt); break;
        case INSTR_CHIPTUNE: x = chip(v, n, dt); break;
        case INSTR_SINO:     x = bell(v, dt); break;
        case INSTR_BATERIA:
        default:             x = drum(v, n); break;
        }
        if (v->instrument != INSTR_BATERIA) x *= env_at(v, n);
        x *= v->amp;
        left[i] += x * v->gain_l;
        right[i] += x * v->gain_r;
        send[i] += x * v->send;
    }
}
