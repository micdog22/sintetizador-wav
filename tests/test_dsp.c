/*
 * test_dsp.c: matemática, osciladores (incluindo a medida de aliasing do
 * PolyBLEP), envelope, filtros, pan, limitador e reverberação.
 */
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "dsp.h"
#include "teste.h"

#define N SAMPLE_RATE /* 1 segundo: com frequência inteira, cada harmônico cai num bin exato */
#define SQRT1_2 0.70710678118654752440

static double absd(double x)
{
    return x < 0 ? -x : x;
}

static void test_math(void)
{
    double worst = 0.0;
    for (double x = -3.0; x <= 3.0; x += 0.000731) {
        double e = absd(dsp_sin2pi(x) - sin(2.0 * DSP_PI * x));
        if (e > worst) worst = e;
    }
    /* A referência calcula sin(2 * pi * x) e já erra uns 1e-15 ao arredondar
     * o argumento, então a tolerância não pode ser menor que isso. */
    printf("  maior diferença para a libm: seno %.1e\n", worst);
    CHECK(worst < 4e-15);
    CHECK_EQ(dsp_sin2pi(0.0), 0.0);
    CHECK(absd(dsp_sin2pi(0.25) - 1.0) < 4e-16);
    CHECK(absd(dsp_cos2pi(0.0) - 1.0) < 4e-16);

    worst = 0.0;
    for (double x = -40.0; x <= 40.0; x += 0.0137) {
        double e = absd(dsp_exp2(x) / exp2(x) - 1.0);
        if (e > worst) worst = e;
    }
    CHECK(worst < 1e-14);
    CHECK(dsp_exp2(3.0) == 8.0);       /* potências inteiras são exatas */
    CHECK(dsp_exp2(-1.0) == 0.5);
    CHECK(dsp_exp2(-2000.0) == 0.0);

    worst = 0.0;
    for (double x = -20.0; x <= 20.0; x += 0.0071) {
        double e = absd(dsp_exp(x) / exp(x) - 1.0);
        if (e > worst) worst = e;
    }
    CHECK(worst < 1e-13);

    worst = 0.0;
    for (double x = -25.0; x <= 25.0; x += 0.0093) {
        double e = absd(dsp_tanh(x) - tanh(x));
        if (e > worst) worst = e;
    }
    CHECK(worst < 1e-14);
}

static void test_note_frequency(void)
{
    /* A4 = 440 Hz e cada oitava dobra: valores exatos. */
    CHECK(note_frequency(69) == 440.0);
    CHECK(note_frequency(81) == 880.0);
    CHECK(note_frequency(57) == 220.0);
    CHECK(note_frequency(21) == 27.5);           /* A0, a tecla mais grave do piano */
    CHECK(absd(note_frequency(60) - 261.6255653005986) < 1e-9);  /* C4, dó central */
    CHECK(absd(note_frequency(108) - 4186.009044809578) < 1e-9); /* C8 */
    CHECK(absd(note_frequency(0) - 8.175798915643707) < 1e-12);
    double ratio = exp2(1.0 / 12.0), worst = 0.0;
    for (int n = 0; n < 127; n++) {
        double f = note_frequency(n);
        double e = absd(f / (440.0 * pow(2.0, (n - 69) / 12.0)) - 1.0);
        if (e > worst) worst = e;
        CHECK(absd(note_frequency(n + 1) / f - ratio) < 1e-12);  /* um semitom */
    }
    CHECK(worst < 1e-14);
}

/* FFT radix-2 iterativa (Cooley-Tukey), só para os testes. */
#define FN 65536
static double fre[FN], fim[FN];

static void fft(void)
{
    for (unsigned i = 1, j = 0; i < FN; i++) {
        unsigned bit = FN >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = fre[i];
            fre[i] = fre[j];
            fre[j] = t;
            t = fim[i];
            fim[i] = fim[j];
            fim[j] = t;
        }
    }
    for (unsigned len = 2; len <= FN; len <<= 1) {
        for (unsigned i = 0; i < FN; i += len) {
            for (unsigned k = 0; k < len / 2; k++) {
                double wr = dsp_cos2pi((double)k / len), wi = -dsp_sin2pi((double)k / len);
                unsigned a = i + k, b = a + len / 2;
                double tr = fre[b] * wr - fim[b] * wi, ti = fre[b] * wi + fim[b] * wr;
                fre[b] = fre[a] - tr;
                fim[b] = fim[a] - ti;
                fre[a] += tr;
                fim[a] += ti;
            }
        }
    }
}

/*
 * Mede o aliasing: gera FN amostras de uma onda cuja frequência cai num bin
 * exato da FFT (kf0 ciclos em FN amostras, sem vazamento espectral). Tudo o
 * que não estiver nos múltiplos de kf0 é alias: harmônicos acima de Nyquist
 * que "dobraram" para frequências erradas. Devolve alias / harmônicos (dB)
 * até max_hz.
 */
static double alias_db(int kind, int kf0, double max_hz)
{
    double dt = (double)kf0 / FN, phase = 0.0;
    for (int n = 0; n < FN; n++) {
        switch (kind) {
        case 0: fre[n] = osc_saw_naive(phase); break;
        case 1: fre[n] = osc_saw(phase, dt); break;
        case 2: fre[n] = osc_square_naive(phase, 0.25); break;
        default: fre[n] = osc_square(phase, dt, 0.25); break;
        }
        fim[n] = 0.0;
        phase += dt;
        if (phase >= 1.0) phase -= 1.0;
    }
    fft();
    int kmax = (int)(max_hz * FN / SAMPLE_RATE);
    double alias = 0.0, harm = 0.0;
    for (int k = 1; k <= kmax && k <= FN / 2; k++) {
        double p = fre[k] * fre[k] + fim[k] * fim[k];
        if (k % kf0 == 0) harm += p;
        else alias += p;
    }
    return 10.0 * log10(alias / harm);
}

static void test_polyblep(void)
{
    const int kf0 = 1834; /* 1234,1 Hz: nota aguda, muitos harmônicos acima de 22050 Hz */
    double saw_naive = alias_db(0, kf0, 5000.0), saw_blep = alias_db(1, kf0, 5000.0);
    double sq_naive = alias_db(2, kf0, 5000.0), sq_blep = alias_db(3, kf0, 5000.0);
    printf("  aliasing até 5 kHz, nota de 1234 Hz: serra %.1f dB -> %.1f dB, pulso %.1f dB -> %.1f dB\n",
           saw_naive, saw_blep, sq_naive, sq_blep);
    CHECK(saw_naive > -25.0);            /* sem correção, o alias é audível */
    CHECK(saw_blep < saw_naive - 40.0);  /* PolyBLEP tira mais de 40 dB */
    CHECK(sq_blep < sq_naive - 40.0);
    /* Na banda inteira o ganho é menor: o resíduo fica perto de Nyquist. */
    double full_naive = alias_db(0, kf0, 22050.0), full_blep = alias_db(1, kf0, 22050.0);
    printf("  aliasing na banda toda: serra %.1f dB -> %.1f dB\n", full_naive, full_blep);
    CHECK(full_blep < full_naive - 12.0);

    /* O pulso não tem nível DC, qualquer que seja a largura. */
    double mean = 0.0, phase = 0.0, dt = 440.0 / SAMPLE_RATE;
    for (int n = 0; n < N; n++) {
        mean += osc_square(phase, dt, 0.25);
        phase += dt;
        if (phase >= 1.0) phase -= 1.0;
    }
    CHECK(absd(mean / N) < 1e-3);

    /* Longe dos saltos, PolyBLEP não muda nada. */
    CHECK(poly_blep(0.5, 0.01) == 0.0);
    CHECK(osc_saw(0.5, 0.01) == osc_saw_naive(0.5));
}

static void test_triangle_and_noise(void)
{
    tri_osc t;
    tri_init(&t);
    double dt = 440.0 / SAMPLE_RATE, lo = 0.0, hi = 0.0, mean = 0.0;
    for (int n = 0; n < N; n++) {
        double v = tri_next(&t, dt);
        if (n > N / 2) {
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            mean += v;
        }
    }
    CHECK(hi > 0.97 && hi < 1.03);
    CHECK(lo < -0.97 && lo > -1.03);
    CHECK(absd(mean / (N / 2)) < 0.01);

    uint32_t s = 1;
    double sum = 0.0, sq = 0.0, mn = 1.0, mx = -1.0;
    for (int n = 0; n < N; n++) {
        double v = noise_next(&s);
        sum += v;
        sq += v * v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    CHECK(absd(sum / N) < 0.01);
    CHECK(absd(sq / N - 1.0 / 3.0) < 0.01); /* variância da uniforme em [-1, 1] */
    CHECK(mn >= -1.0 && mx < 1.0);
    uint32_t a = 42, b = 42;
    CHECK(noise_next(&a) == noise_next(&b)); /* mesma semente, mesmo ruído */
}

static void test_adsr(void)
{
    adsr e = { 0.1, 0.2, 0.5, 0.4 };
    double off = 1.0;
    CHECK(absd(adsr_level(&e, 0.0, off) - 0.0) < 1e-12);
    CHECK(absd(adsr_level(&e, 0.05, off) - 0.5) < 1e-12);   /* meio do ataque */
    CHECK(absd(adsr_level(&e, 0.1, off) - 1.0) < 1e-12);    /* pico */
    CHECK(absd(adsr_level(&e, 0.2, off) - 0.75) < 1e-12);   /* meio do decaimento */
    CHECK(absd(adsr_level(&e, 0.3, off) - 0.5) < 1e-12);    /* chegou na sustentação */
    CHECK(absd(adsr_level(&e, 0.8, off) - 0.5) < 1e-12);
    CHECK(absd(adsr_level(&e, 1.0, off) - 0.5) < 1e-12);    /* soltou */
    CHECK(absd(adsr_level(&e, 1.2, off) - 0.25) < 1e-12);   /* meio do release */
    CHECK(adsr_level(&e, 1.4, off) < 1e-12);
    CHECK(adsr_level(&e, 5.0, off) == 0.0);
    CHECK(adsr_level(&e, -1.0, off) == 0.0);
    /* Soltar no meio do ataque: o release parte do nível atual, sem degrau. */
    CHECK(absd(adsr_level(&e, 0.05, 0.05) - 0.5) < 1e-12);
    CHECK(absd(adsr_level(&e, 0.25, 0.05) - 0.25) < 1e-12);
    /* Sem ataque, começa no máximo. */
    adsr instant = { 0.0, 0.0, 1.0, 0.1 };
    CHECK(adsr_level(&instant, 0.0, 1.0) == 1.0);
}

/* Amplitude de pico de uma senoide de frequência f depois do filtro. */
static double filtered_amplitude(biquad *bq, onepole *op, double f)
{
    double peak = 0.0;
    for (int n = 0; n < N; n++) {
        double x = dsp_sin2pi(f * n / SAMPLE_RATE);
        double y = bq ? biquad_run(bq, x) : onepole_run(op, x);
        if (n > N / 2 && absd(y) > peak) peak = absd(y);
    }
    return peak;
}

static void test_filters(void)
{
    onepole op = { 0.0, 0.0 };
    onepole_set(&op, 100.0);
    double a = filtered_amplitude(NULL, &op, 100.0);
    CHECK(a > 0.69 && a < 0.72);                 /* -3 dB na frequência de corte */
    op.y = 0.0;
    for (int n = 0; n < N; n++) onepole_run(&op, 1.0);
    CHECK(absd(op.y - 1.0) < 1e-9);              /* ganho 1 em DC */

    biquad lp = { 0 };
    biquad_set(&lp, BQ_LOWPASS, 1000.0, SQRT1_2);
    CHECK(absd(biquad_gain(&lp, 1000.0) - SQRT1_2) < 1e-9);   /* no corte, ganho = Q */
    CHECK(absd(biquad_gain(&lp, 1.0) - 1.0) < 1e-6);
    CHECK(biquad_gain(&lp, 10000.0) < 0.012);    /* 12 dB por oitava: -40 dB uma década acima */
    double sim = filtered_amplitude(&lp, NULL, 1000.0);
    CHECK(absd(sim - SQRT1_2) < 0.005);        /* a simulação confere com a conta */

    biquad hp = { 0 };
    biquad_set(&hp, BQ_HIGHPASS, 1000.0, SQRT1_2);
    CHECK(absd(biquad_gain(&hp, 1000.0) - SQRT1_2) < 1e-9);
    CHECK(biquad_gain(&hp, 50.0) < 0.003);
    CHECK(absd(biquad_gain(&hp, 15000.0) - 1.0) < 0.01);

    biquad bp = { 0 };
    biquad_set(&bp, BQ_BANDPASS, 2000.0, 2.0);
    CHECK(absd(biquad_gain(&bp, 2000.0) - 1.0) < 1e-9); /* pico de 0 dB no centro */
    CHECK(biquad_gain(&bp, 500.0) < 0.2);
    CHECK(biquad_gain(&bp, 8000.0) < 0.2);
}

static void test_pan_and_clip(void)
{
    double l, r;
    pan_gains(0.0, &l, &r);
    CHECK(absd(l - SQRT1_2) < 1e-12 && absd(r - SQRT1_2) < 1e-12);
    pan_gains(-1.0, &l, &r);
    CHECK(absd(l - 1.0) < 1e-12 && absd(r) < 1e-12);
    pan_gains(1.0, &l, &r);
    CHECK(absd(l) < 1e-12 && absd(r - 1.0) < 1e-12);
    for (double p = -1.0; p <= 1.0; p += 0.05) {
        pan_gains(p, &l, &r);
        CHECK(absd(l * l + r * r - 1.0) < 1e-12); /* potência constante */
    }

    CHECK(soft_clip(0.5) == 0.5);
    CHECK(soft_clip(-0.75) == -0.75);
    CHECK(absd(soft_clip(0.7500001) - 0.7500001) < 1e-9);   /* contínuo no joelho */
    double prev = -2.0;
    bool monotonic = true, bounded = true;
    for (double x = -50.0; x <= 50.0; x += 0.001) {
        double y = soft_clip(x);
        if (y < prev) monotonic = false;
        if (absd(y) > 1.0) bounded = false;
        if (absd(soft_clip(-x) + y) > 1e-15) bounded = false;   /* função ímpar */
        prev = y;
    }
    CHECK(monotonic);
    CHECK(bounded);
    CHECK(soft_clip(1e9) <= 1.0);

    CHECK_EQ(to_int16(1.0), 32767);
    CHECK_EQ(to_int16(-1.0), -32767);
    CHECK_EQ(to_int16(0.0), 0);
    CHECK_EQ(to_int16(0.5), 16384);
    CHECK_EQ(to_int16(3.0), 32767);
    CHECK_EQ(to_int16(-3.0), -32767);
    CHECK_EQ(to_int16(NAN), 0);
}

static void test_reverb(void)
{
    /* Passa-tudo: a energia da resposta ao impulso é exatamente a da entrada. */
    double buf[100] = { 0 };
    allpass_filter ap = { buf, 100, 0, 0.7 };
    double energy = 0.0;
    for (int n = 0; n < 20000; n++) {
        double y = allpass_run(&ap, n == 0 ? 1.0 : 0.0);
        energy += y * y;
    }
    CHECK(absd(energy - 1.0) < 1e-9);

    /* Pente: cada volta multiplica pelo ganho de realimentação. */
    double cbuf[50] = { 0 };
    comb_filter cf = { cbuf, 50, 0, 0.5, 0.0, 0.0 };
    double first = 0.0, third = 0.0;
    for (int n = 0; n < 200; n++) {
        double y = comb_run(&cf, n == 0 ? 1.0 : 0.0);
        if (n == 50) first = y;
        if (n == 150) third = y;
    }
    CHECK(absd(first - 1.0) < 1e-12);
    CHECK(absd(third - 0.25) < 1e-12);

    /* Reverberação completa: decai uns 60 dB em rt60 segundos. */
    reverb rv;
    CHECK_EQ(reverb_init(&rv, 1.0, 0.0), 0);
    double early = 0.0, late = 0.0, diff = 0.0;
    bool finite = true;
    for (int n = 0; n < 2 * SAMPLE_RATE; n++) {
        double l, r;
        reverb_run(&rv, n == 0 ? 1.0 : 0.0, &l, &r);
        if (!(l == l) || !(r == r)) finite = false;
        if (n < SAMPLE_RATE / 10) early += l * l;
        else if (n >= SAMPLE_RATE && n < SAMPLE_RATE + SAMPLE_RATE / 10) late += l * l;
        diff += (l - r) * (l - r);
    }
    reverb_free(&rv);
    CHECK(finite);
    CHECK(early > 0.0);
    double drop = 10.0 * log10(early / late);
    printf("  reverberação (rt60 = 1 s): queda de %.1f dB entre 0 s e 1 s\n", drop);
    CHECK(drop > 50.0 && drop < 75.0);
    CHECK(diff > 0.0); /* os canais são diferentes: estéreo */
}

int main(void)
{
    test_math();
    test_note_frequency();
    test_polyblep();
    test_triangle_and_noise();
    test_adsr();
    test_filters();
    test_pan_and_clip();
    test_reverb();
    return teste_resumo("test_dsp");
}
