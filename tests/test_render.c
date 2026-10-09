/*
 * test_render.c: tempo em amostras, formato WAV, síntese de ponta a ponta
 * (altura de cada instrumento, pan, silêncio, limites de 16 bits) e o hash
 * determinístico de uma renderização curta.
 */
#include <math.h>
#include <stdlib.h>

#include "partitura.h"
#include "render.h"
#include "teste.h"
#include "wav.h"

/* Renderiza a partitura inteira pedindo "chunk" quadros por vez. */
static int16_t *render(const char *src, bool reverb, size_t chunk, long *frames)
{
    song s;
    char err[256];
    if (song_parse(src, "t", &s, err, sizeof err) != 0) {
        fprintf(stderr, "%s\n", err);
        return NULL;
    }
    renderer r;
    if (renderer_init(&r, &s, reverb) != 0) {
        song_free(&s);
        return NULL;
    }
    int16_t *out = malloc((size_t)r.total * 2 * sizeof *out);
    size_t done = 0, got;
    while (out && (got = renderer_run(&r, out + 2 * done, chunk)) > 0) done += got;
    *frames = (long)done;
    if (done != (size_t)r.total) {
        free(out);
        out = NULL;
    }
    renderer_free(&r);
    song_free(&s);
    return out;
}

static uint64_t fnv(const int16_t *x, long frames)
{
    uint64_t h = 0xcbf29ce484222325u;
    uint8_t b[4];
    for (long i = 0; i < frames; i++) {
        wav_pack(x + 2 * i, 2, b);
        for (int k = 0; k < 4; k++) {
            h ^= b[k];
            h *= 0x100000001b3u;
        }
    }
    return h;
}

static void test_timing(void)
{
    song s;
    memset(&s, 0, sizeof s);
    s.tempo = 120;                                   /* semínima = 0,5 s */
    CHECK_EQ(ticks_to_samples(&s, TICKS_QUARTER), 22050);
    CHECK_EQ(ticks_to_samples(&s, TICKS_WHOLE * 60), 44100 * 120);
    CHECK_EQ(ticks_to_samples(&s, 64), 7350);        /* colcheia de quiáltera */
    s.tempo = 100;
    CHECK_EQ(ticks_to_samples(&s, TICKS_QUARTER), 26460);
    s.tempo = 90;
    CHECK_EQ(ticks_to_samples(&s, 96), 14700);
    s.tempo = 7 * 13;                                /* arredonda para a amostra mais próxima */
    CHECK_EQ(ticks_to_samples(&s, 1), 151);          /* 151,44... */
}

static void test_wav_format(void)
{
    static const uint8_t expected[WAV_HEADER_SIZE] = {
        'R', 'I', 'F', 'F', 0xC4, 0x0F, 0x00, 0x00, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00,
        0x44, 0xAC, 0x00, 0x00,  /* 44100 */
        0x10, 0xB1, 0x02, 0x00,  /* 176400 bytes por segundo */
        0x04, 0x00, 0x10, 0x00,  /* 4 bytes por quadro, 16 bits */
        'd', 'a', 't', 'a', 0xA0, 0x0F, 0x00, 0x00   /* 1000 quadros = 4000 bytes */
    };
    uint8_t h[WAV_HEADER_SIZE];
    wav_header(h, 1000, 2, 44100);
    CHECK(memcmp(h, expected, sizeof h) == 0);

    const int16_t samples[5] = { 1, -1, 256, -32768, 32767 };
    uint8_t b[10];
    wav_pack(samples, 5, b);
    static const uint8_t le[10] = { 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x01, 0x00, 0x80, 0xFF, 0x7F };
    CHECK(memcmp(b, le, sizeof b) == 0);
}

/* Magnitude da DFT de x (canal esquerdo) na frequência f. */
static double dft_mag(const int16_t *x, long from, long count, double f)
{
    double re = 0.0, im = 0.0;
    for (long n = 0; n < count; n++) {
        double ph = f * (double)n / SAMPLE_RATE;
        re += x[2 * (from + n)] * dsp_cos2pi(ph);
        im -= x[2 * (from + n)] * dsp_sin2pi(ph);
    }
    return sqrt(re * re + im * im);
}

static void test_pitch_per_instrument(void)
{
    static const char *const names[] = { "piano", "orgao", "baixo", "flauta", "chiptune", "sino" };
    for (size_t k = 0; k < sizeof names / sizeof names[0]; k++) {
        char src[128];
        snprintf(src, sizeof src, "tempo 60\ninstrumento %s\nA4:1/2", names[k]);
        long frames;
        int16_t *x = render(src, false, 4096, &frames);
        CHECK(x != NULL);
        if (!x) continue;
        /* 0,5 s a partir de 0,1 s: o lá de 440 Hz domina os vizinhos de um semitom. */
        double at = dft_mag(x, 4410, 22050, 440.0);
        double below = dft_mag(x, 4410, 22050, 415.3), above = dft_mag(x, 4410, 22050, 466.2);
        if (!(at > 5.0 * below && at > 5.0 * above)) {
            fprintf(stderr, "  %s: 440 Hz %.0f, 415 Hz %.0f, 466 Hz %.0f\n", names[k], at, below, above);
        }
        CHECK(at > 5.0 * below && at > 5.0 * above);
        free(x);
    }
}

static void test_structure(void)
{
    long frames;
    /* Duração: nota até o fim do release, mais a cauda da reverberação. */
    int16_t *x = render("tempo 120\nC4:1/4", true, 4096, &frames);
    CHECK(x != NULL);
    CHECK_EQ(frames, 22050 + (long)(0.30 * SAMPLE_RATE) + 1 + (long)(1.6 * SAMPLE_RATE));
    free(x);
    x = render("tempo 120\nC4:1/4", false, 4096, &frames);
    CHECK_EQ(frames, 22050 + (long)(0.30 * SAMPLE_RATE) + 1 + (long)(0.1 * SAMPLE_RATE));
    free(x);

    /* Antes da primeira nota, silêncio absoluto (zeros), mesmo com reverberação. */
    x = render("tempo 120\nr:1/2 C4:1/4", true, 4096, &frames);
    bool silent = true, sound = false;
    for (long i = 0; i < 44100; i++) {
        if (x[2 * i] || x[2 * i + 1]) silent = false;
    }
    for (long i = 44100; i < frames; i++) {
        if (x[2 * i]) sound = true;
    }
    CHECK(silent);
    CHECK(sound);
    free(x);

    /* Pan todo para a esquerda: o canal direito fica mudo. */
    x = render("instrumento flauta\npan -100\nC5:1/2 E5", false, 4096, &frames);
    bool right_silent = true, left_sound = false;
    for (long i = 0; i < frames; i++) {
        if (x[2 * i + 1] != 0) right_silent = false;
        if (x[2 * i] != 0) left_sound = true;
    }
    CHECK(right_silent && left_sound);
    free(x);
}

/* Uma partitura com todos os instrumentos e quase tudo da sintaxe. */
static const char *const SHORT_SONG =
    "tempo 132\n"
    "compasso 4/4\n"
    "faixa a\ninstrumento piano\npan -40\n"
    "mf C4:1/8 E4 G4 C5 [C4 E4 G4]:1/2 |\n"
    "faixa b\ninstrumento orgao\n[C3 G3]:1 |\n"
    "faixa c\ninstrumento baixo\nC2:1/4. C3:1/8 G2:1/4t G2 G2 |\n"
    "faixa d\ninstrumento flauta\npan 50\nr:1/2 G5:1/4~ G5:1/8 A5:1/8 |\n"
    "faixa e\ninstrumento chiptune\nE5:1/16 G5 C6 G5 E5:1/8. r:1/16 C5:1/2 |\n"
    "faixa f\ninstrumento sino\nreverb 60\nff C6:1/2 G5:1/2 |\n"
    "faixa g\ninstrumento bateria\n[bumbo chimbal]:1/8 chimbal [caixa chimbal] chimbal_aberto bumbo bumbo caixa prato |\n";

static void test_determinism(void)
{
    long f1, f2, f3, f4;
    int16_t *a = render(SHORT_SONG, true, 4096, &f1);
    int16_t *b = render(SHORT_SONG, true, 1, &f2);      /* um quadro por vez */
    int16_t *c = render(SHORT_SONG, true, 777, &f3);    /* blocos que não batem com 512 */
    int16_t *d = render(SHORT_SONG, true, 4096, &f4);
    CHECK(a && b && c && d);
    if (a && b && c && d) {
        CHECK_EQ(f1, f2);
        CHECK_EQ(f1, f3);
        CHECK(memcmp(a, b, (size_t)f1 * 4) == 0);
        CHECK(memcmp(a, c, (size_t)f1 * 4) == 0);
        CHECK(memcmp(a, d, (size_t)f1 * 4) == 0);
        uint64_t h = fnv(a, f1);
        printf("  hash da renderização curta: %016llx (%ld quadros)\n", (unsigned long long)h, f1);
        /* Mesmo valor em qualquer máquina IEEE 754: seno e exponencial são
         * calculados por polinômios próprios e o build usa -ffp-contract=off. */
        CHECK_EQ(h, 0x3d44620c09f6a468ULL);
    }
    free(a);
    free(b);
    free(c);
    free(d);
}

static void test_int16_limits(void)
{
    /* Seis faixas em ff tocando acordes graves: a mistura passa de 1,0 e o
     * limitador precisa segurar sem estourar os 16 bits. */
    char src[2048] = "tempo 120\n";
    static const char *const instr[] = { "baixo", "orgao", "piano", "chiptune", "baixo", "orgao" };
    for (int k = 0; k < 6; k++) {
        char part[256];
        snprintf(part, sizeof part, "faixa f%d\ninstrumento %s\nvolume 100\nff [C2 E2 G2 C3 E3 G3]:1 |\n", k, instr[k]);
        strcat(src, part);
    }
    long frames;
    int16_t *x = render(src, true, 4096, &frames);
    CHECK(x != NULL);
    if (!x) return;
    int peak = 0;
    long hot = 0;
    for (long i = 0; i < 2 * frames; i++) {
        int v = x[i] < 0 ? -x[i] : x[i];
        if (v > peak) peak = v;
        if (v > 24575) hot++;      /* acima de 0,75: o limitador está agindo */
    }
    CHECK(peak <= 32767);
    CHECK(peak > 30000);
    CHECK(hot > 1000);
    bool no_min = true;
    for (long i = 0; i < 2 * frames; i++) {
        if (x[i] == -32768) no_min = false;   /* o mínimo usado é -32767, simétrico */
    }
    CHECK(no_min);
    free(x);
}

int main(void)
{
    test_timing();
    test_wav_format();
    test_pitch_per_instrument();
    test_structure();
    test_determinism();
    test_int16_limits();
    return teste_resumo("test_render");
}
