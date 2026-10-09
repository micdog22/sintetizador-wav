/*
 * main.c: o programa sintetizador.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "partitura.h"
#include "render.h"
#include "wav.h"

#define VERSION "1.0.0"
#define MAX_SCORE (4 * 1024 * 1024)
#define CHUNK 4096

static void usage(FILE *f)
{
    fputs("uso: sintetizador [opções] partitura.txt\n"
          "\n"
          "Transforma uma partitura em texto num arquivo WAV (44,1 kHz, 16 bits, estéreo).\n"
          "\n"
          "Opções:\n"
          "  -o, --saida ARQ   arquivo WAV de saída (padrão: o nome da partitura com .wav)\n"
          "      --info        mostra tempo, compasso, faixas e duração, sem gerar áudio\n"
          "      --hash        imprime o hash FNV-1a das amostras geradas\n"
          "      --sem-reverb  desliga a reverberação\n"
          "  -h, --help        mostra esta ajuda\n"
          "  -v, --versao      mostra a versão\n",
          f);
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return NULL;
    }
    char *buf = malloc(MAX_SCORE + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "sintetizador: memória insuficiente\n");
        return NULL;
    }
    size_t n = fread(buf, 1, MAX_SCORE, f);
    bool too_big = fgetc(f) != EOF;
    fclose(f);
    if (too_big) {
        fprintf(stderr, "sintetizador: %s passa de 4 MB\n", path);
        free(buf);
        return NULL;
    }
    if (memchr(buf, '\0', n)) {
        fprintf(stderr, "sintetizador: %s não parece ser um arquivo de texto\n", path);
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

static void format_time(double seconds, char *buf, size_t cap)
{
    int min = (int)(seconds / 60.0);
    snprintf(buf, cap, "%d:%04.1f", min, seconds - 60.0 * min);
}

static void print_info(const song *s)
{
    char t[32];
    format_time(song_seconds(s, song_length(s)), t, sizeof t);
    printf("tempo %d (semínimas por minuto), compasso %d/%d, duração %s\n", s->tempo, s->beats, s->unit, t);
    for (int k = 0; k < s->ntracks; k++) {
        const track_info *tr = &s->tracks[k];
        printf("  faixa %-12s %-9s %4d notas  volume %3d  pan %4d  reverb %3d\n", tr->name,
               instrument_name(tr->instrument), tr->nnotes, tr->volume, tr->pan, tr->reverb);
    }
}

int main(int argc, char **argv)
{
    const char *in = NULL, *out = NULL;
    bool info = false, hash = false, reverb_on = true;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--versao") == 0) {
            printf("sintetizador %s\n", VERSION);
            return 0;
        } else if ((strcmp(a, "-o") == 0 || strcmp(a, "--saida") == 0) && i + 1 < argc) {
            out = argv[++i];
        } else if (strcmp(a, "--info") == 0) {
            info = true;
        } else if (strcmp(a, "--hash") == 0) {
            hash = true;
        } else if (strcmp(a, "--sem-reverb") == 0) {
            reverb_on = false;
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "sintetizador: opção desconhecida \"%s\"\n\n", a);
            usage(stderr);
            return 2;
        } else if (!in) {
            in = a;
        } else {
            fprintf(stderr, "sintetizador: informe uma partitura só\n");
            return 2;
        }
    }
    if (!in) {
        usage(stderr);
        return 2;
    }

    char *src = read_file(in);
    if (!src) return 1;
    song s;
    char err[512];
    int rc = song_parse(src, in, &s, err, sizeof err);
    free(src);
    if (rc != 0) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    if (info) {
        print_info(&s);
        song_free(&s);
        return 0;
    }

    char path[4096];
    if (!out) {
        const char *dot = strrchr(in, '.'), *slash = strrchr(in, '/');
        size_t base = (dot && (!slash || dot > slash)) ? (size_t)(dot - in) : strlen(in);
        if (base + 5 > sizeof path) {
            fprintf(stderr, "sintetizador: caminho longo demais\n");
            song_free(&s);
            return 1;
        }
        memcpy(path, in, base);
        memcpy(path + base, ".wav", 5);
        out = path;
    }

    renderer r;
    if (renderer_init(&r, &s, reverb_on) != 0) {
        fprintf(stderr, "sintetizador: memória insuficiente\n");
        song_free(&s);
        return 1;
    }
    if ((unsigned long)r.total > WAV_MAX_FRAMES) {
        fprintf(stderr, "sintetizador: áudio longo demais para um WAV\n");
        renderer_free(&r);
        song_free(&s);
        return 1;
    }
    FILE *f = fopen(out, "wb");
    if (!f) {
        perror(out);
        renderer_free(&r);
        song_free(&s);
        return 1;
    }

    uint8_t header[WAV_HEADER_SIZE];
    wav_header(header, (uint32_t)r.total, 2, SAMPLE_RATE);
    bool ok = fwrite(header, 1, sizeof header, f) == sizeof header;

    static int16_t samples[CHUNK * 2];
    static uint8_t bytes[CHUNK * 4];
    uint64_t h = 0xcbf29ce484222325u;   /* FNV-1a de 64 bits */
    int peak = 0;
    size_t got;
    while (ok && (got = renderer_run(&r, samples, CHUNK)) > 0) {
        wav_pack(samples, got * 2, bytes);
        for (size_t k = 0; k < got * 4; k++) {
            h ^= bytes[k];
            h *= 0x100000001b3u;
        }
        for (size_t k = 0; k < got * 2; k++) {
            int a = samples[k] < 0 ? -samples[k] : samples[k];
            if (a > peak) peak = a;
        }
        ok = fwrite(bytes, 1, got * 4, f) == got * 4;
    }
    if (fclose(f) != 0) ok = false;
    if (!ok || r.pos != r.total) {
        fprintf(stderr, "sintetizador: erro ao gravar %s\n", out);
        renderer_free(&r);
        song_free(&s);
        return 1;
    }

    char t[32];
    format_time((double)r.total / SAMPLE_RATE, t, sizeof t);
    double dbfs = peak > 0 ? 20.0 * log10(peak / 32767.0) : -INFINITY;
    printf("%s: %s, %d faixa%s, %d notas, pico %.1f dBFS\n", out, t, s.ntracks, s.ntracks == 1 ? "" : "s",
           s.nevents, dbfs);
    if (hash) printf("hash: %016llx\n", (unsigned long long)h);
    renderer_free(&r);
    song_free(&s);
    return 0;
}
