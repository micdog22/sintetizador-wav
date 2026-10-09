/*
 * partitura.c: analisador da partitura em texto.
 *
 * É um analisador descendente recursivo que lê caractere a caractere,
 * guardando linha e coluna para as mensagens de erro. A repetição |: ... :|
 * é feita de um jeito simples: ao chegar em :| pela primeira vez, o cursor
 * volta para logo depois do |: e aquele trecho é lido de novo.
 */
#include "partitura.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WORD    64
#define MAX_CHORD   16
#define MAX_EVENTS  500000
#define MAX_SECONDS 1800.0

static const char *const INSTRUMENTS[INSTR_COUNT] = {
    "piano", "orgao", "baixo", "flauta", "chiptune", "sino", "bateria"
};
static const int DEFAULT_REVERB[INSTR_COUNT] = { 25, 20, 5, 25, 10, 35, 8 };

static const char *const DRUMS[DRUM_COUNT] = {
    "bumbo", "caixa", "chimbal", "chimbal_aberto", "prato"
};

static const struct {
    const char *name;
    double      velocity;
} DYNAMICS[] = {
    { "pp", 0.30 }, { "p", 0.45 }, { "mp", 0.60 }, { "mf", 0.75 }, { "f", 0.90 }, { "ff", 1.00 }
};

typedef struct {
    size_t pos;
    int    line;
    size_t line_start;
} cursor;

typedef struct {
    const char *src;
    cursor      c;
    const char *file;
    song       *s;
    char       *err;
    size_t      errcap;
    bool        failed;
    bool        header_closed;  /* já houve faixa ou nota: tempo e compasso não valem mais */

    /* Estado da faixa atual. */
    int    track;               /* -1 enquanto não houver faixa */
    bool   has_notes;
    bool   reverb_set;
    long   time;                /* ticks */
    double velocity;
    long   last_dur;            /* duração usada quando a nota não diz a sua */
    bool   started;
    cursor track_start;         /* primeiro elemento musical da faixa */
    cursor last_repeat_end;     /* destino de um :| sem |: */

    /* Conferência de compassos. */
    long measure;               /* ticks por compasso */
    bool bar_seen;
    long bar_offset;            /* duração da anacruse */
    long last_bar;

    /* Ligadura aberta (~): eventos que ainda podem crescer. */
    bool tie_open;
    int  tie_first, tie_count, tie_line, tie_col;

    /* Repetição. */
    bool   rep_open, rep_second;
    cursor rep_start;
    int    rep_line, rep_col;
} parser;

/* ---------------------------------------------------------------------------
 * Utilidades
 * ------------------------------------------------------------------------ */

const char *instrument_name(instrument_id id)
{
    return (id >= 0 && id < INSTR_COUNT) ? INSTRUMENTS[id] : "?";
}

const char *drum_name(drum_id id)
{
    return (id >= 0 && id < DRUM_COUNT) ? DRUMS[id] : "?";
}

long song_length(const song *s)
{
    long len = 0;
    for (int k = 0; k < s->ntracks; k++) {
        if (s->tracks[k].length > len) len = s->tracks[k].length;
    }
    return len;
}

double song_seconds(const song *s, long ticks)
{
    return (double)ticks / TICKS_QUARTER * 60.0 / s->tempo;
}

void song_free(song *s)
{
    free(s->tracks);
    free(s->events);
    memset(s, 0, sizeof *s);
}

static int column(const parser *p, size_t pos)
{
    int col = 1;
    for (size_t i = p->c.line_start; i < pos; i++) {
        if (((unsigned char)p->src[i] & 0xC0) != 0x80) col++; /* conta caracteres, não bytes */
    }
    return col;
}

static bool fail(parser *p, int line, int col, const char *fmt, ...)
{
    if (p->failed) return false;
    p->failed = true;
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(p->err, p->errcap, "%s:%d:%d: erro: %s", p->file, line, col, msg);
    return false;
}

static int peek(const parser *p)
{
    return (unsigned char)p->src[p->c.pos];
}

static int peek2(const parser *p)
{
    return p->src[p->c.pos] ? (unsigned char)p->src[p->c.pos + 1] : 0;
}

static void advance(parser *p)
{
    if (p->src[p->c.pos] == '\n') {
        p->c.pos++;
        p->c.line++;
        p->c.line_start = p->c.pos;
    } else if (p->src[p->c.pos]) {
        p->c.pos++;
    }
}

static bool is_word_char(int c)
{
    return isalnum(c) || c == '#' || c == '_' || c >= 0x80;
}

/* Pula espaços, quebras de linha e comentários. Um # só começa comentário no
 * início de uma palavra; dentro dela (C#4) é sustenido. */
static void skip_blank(parser *p)
{
    for (;;) {
        int c = peek(p);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(p);
        } else if (c == '#') {
            while (peek(p) && peek(p) != '\n') advance(p);
        } else {
            return;
        }
    }
}

static void skip_inline_space(parser *p)
{
    while (peek(p) == ' ' || peek(p) == '\t' || peek(p) == '\r') advance(p);
}

static bool read_word(parser *p, char *buf)
{
    int line = p->c.line, col = column(p, p->c.pos);
    size_t n = 0;
    while (is_word_char(peek(p))) {
        if (n + 1 >= MAX_WORD) return fail(p, line, col, "palavra longa demais");
        buf[n++] = (char)peek(p);
        advance(p);
    }
    buf[n] = '\0';
    return true;
}

static bool read_int(parser *p, const char *what, long lo, long hi, long *out)
{
    skip_inline_space(p);
    int line = p->c.line, col = column(p, p->c.pos);
    bool neg = false;
    if (peek(p) == '-') {
        neg = true;
        advance(p);
    }
    if (!isdigit(peek(p))) return fail(p, line, col, "esperava um número para %s", what);
    long v = 0;
    while (isdigit(peek(p))) {
        if (v < 100000000) v = v * 10 + (peek(p) - '0');
        advance(p);
    }
    if (neg) v = -v;
    if (v < lo || v > hi) return fail(p, line, col, "%s deve ficar entre %ld e %ld", what, lo, hi);
    *out = v;
    return true;
}

static long gcd(long a, long b)
{
    while (b) {
        long t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* Duração em ticks escrita como fração de semibreve: 288 vira "3/8". */
static const char *fmt_dur(long ticks, char *buf, size_t cap)
{
    long g = gcd(ticks, TICKS_WHOLE);
    if (g == 0) g = 1;
    if (TICKS_WHOLE / g == 1) {
        snprintf(buf, cap, "%ld", ticks / g);
    } else {
        snprintf(buf, cap, "%ld/%ld", ticks / g, TICKS_WHOLE / g);
    }
    return buf;
}

/* ---------------------------------------------------------------------------
 * Alturas e durações
 * ------------------------------------------------------------------------ */

/* Compara o começo de w com name, sem diferenciar maiúsculas nas letras ASCII. */
static size_t prefix_nocase(const char *w, const char *name)
{
    size_t i = 0;
    for (; name[i]; i++) {
        unsigned char a = (unsigned char)w[i], b = (unsigned char)name[i];
        if (a == 0) return 0;
        if (a < 0x80 && b < 0x80) {
            if (tolower(a) != tolower(b)) return 0;
        } else if (a != b) {
            return 0;
        }
    }
    return i;
}

int parse_pitch_word(const char *w, int *midi)
{
    /* Nomes do solfejo (com ou sem acento) e letras (notação inglesa). */
    static const struct {
        const char *name;
        int         pc;
    } SOLFEGE[] = {
        { "sol", 7 }, { "d\xC3\xB3", 0 }, { "do", 0 }, { "r\xC3\xA9", 2 }, { "re", 2 },
        { "mi", 4 }, { "f\xC3\xA1", 5 }, { "fa", 5 }, { "l\xC3\xA1", 9 }, { "la", 9 }, { "si", 11 }
    };
    static const int LETTER[7] = { 9, 11, 0, 2, 4, 5, 7 }; /* A B C D E F G */

    size_t i = 0;
    int pc = -1;
    for (size_t k = 0; k < sizeof SOLFEGE / sizeof SOLFEGE[0]; k++) {
        size_t n = prefix_nocase(w, SOLFEGE[k].name);
        if (n) {
            pc = SOLFEGE[k].pc;
            i = n;
            break;
        }
    }
    if (pc < 0) {
        int c = tolower((unsigned char)w[0]);
        if (c < 'a' || c > 'g') return -1;
        pc = LETTER[c - 'a'];
        i = 1;
    }
    int acc = 0;
    if (w[i] == '#') {
        while (w[i] == '#') {
            acc++;
            i++;
        }
    } else {
        while (w[i] == 'b') {
            acc--;
            i++;
        }
    }
    if (acc > 2 || acc < -2) return -1;
    if (!isdigit((unsigned char)w[i]) || w[i + 1] != '\0') return -1;
    int octave = w[i] - '0';
    int m = 12 * (octave + 1) + pc + acc; /* C4 = 60, A4 = 69 */
    if (m < 0 || m > 127) return -1;
    *midi = m;
    return 0;
}

static int drum_index(const char *w)
{
    for (int k = 0; k < DRUM_COUNT; k++) {
        if (strlen(w) == strlen(DRUMS[k]) && prefix_nocase(w, DRUMS[k])) return k;
    }
    return -1;
}

/* Lê "n", "n/d", com até dois pontos e um t de quiáltera, a partir do cursor. */
static bool parse_duration_at(parser *p, long *ticks)
{
    int line = p->c.line, col = column(p, p->c.pos);
    if (!isdigit(peek(p))) {
        return fail(p, line, col, "duração inválida: use algo como 1/4, 1/8. ou 1/8t");
    }
    long n = 0, d = 1;
    while (isdigit(peek(p))) {
        if (n < 100000) n = n * 10 + (peek(p) - '0');
        advance(p);
    }
    if (peek(p) == '/') {
        advance(p);
        if (!isdigit(peek(p))) return fail(p, line, col, "duração inválida: faltou o denominador");
        d = 0;
        while (isdigit(peek(p))) {
            if (d < 100000) d = d * 10 + (peek(p) - '0');
            advance(p);
        }
    }
    if (n < 1 || n > 64) return fail(p, line, col, "o numerador da duração vai de 1 a 64");
    if (d < 1 || d > 64 || (d & (d - 1)) != 0) {
        return fail(p, line, col, "o denominador da duração deve ser 1, 2, 4, 8, 16, 32 ou 64");
    }
    long t = TICKS_WHOLE * n / d;
    int dots = 0;
    while (peek(p) == '.') {
        dots++;
        advance(p);
    }
    if (dots > 2) return fail(p, line, col, "no máximo dois pontos de aumento");
    if (dots == 1) t = t * 3 / 2;        /* ponto: mais metade */
    if (dots == 2) t = t * 7 / 4;        /* dois pontos: mais metade e mais um quarto */
    if (peek(p) == 't') {                /* quiáltera: três no lugar de dois */
        advance(p);
        t = t * 2 / 3;
    }
    if (is_word_char(peek(p))) return fail(p, line, col, "duração inválida");
    *ticks = t;
    return true;
}

int parse_duration_text(const char *text, long *ticks)
{
    char err[256];
    song s;
    memset(&s, 0, sizeof s);
    parser p;
    memset(&p, 0, sizeof p);
    p.src = text;
    p.c.line = 1;
    p.file = "";
    p.s = &s;
    p.err = err;
    p.errcap = sizeof err;
    if (!parse_duration_at(&p, ticks) || peek(&p) != '\0') return -1;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Faixas
 * ------------------------------------------------------------------------ */

static bool finish_track(parser *p)
{
    if (p->track < 0) return true;
    if (p->rep_open) {
        return fail(p, p->rep_line, p->rep_col, "a repetição aberta aqui não foi fechada com :|");
    }
    if (p->tie_open) {
        return fail(p, p->tie_line, p->tie_col, "a ligadura (~) desta nota não tem nota seguinte");
    }
    p->s->tracks[p->track].length = p->time;
    return true;
}

static bool start_track(parser *p, const char *name, int line, int col)
{
    if (!finish_track(p)) return false;
    song *s = p->s;
    track_info *grown = realloc(s->tracks, (size_t)(s->ntracks + 1) * sizeof *grown);
    if (!grown) return fail(p, line, col, "memória insuficiente");
    s->tracks = grown;
    track_info *t = &s->tracks[s->ntracks];
    memset(t, 0, sizeof *t);
    snprintf(t->name, sizeof t->name, "%s", name);
    t->instrument = INSTR_PIANO;
    t->volume = 80;
    t->pan = 0;
    t->reverb = DEFAULT_REVERB[INSTR_PIANO];
    p->track = s->ntracks++;

    p->header_closed = true;
    p->has_notes = false;
    p->reverb_set = false;
    p->time = 0;
    p->velocity = 0.75;
    p->last_dur = TICKS_QUARTER;
    p->started = false;
    p->measure = (long)TICKS_WHOLE * s->beats / s->unit;
    p->bar_seen = false;
    p->bar_offset = 0;
    p->last_bar = 0;
    p->tie_open = false;
    p->rep_open = false;
    p->rep_second = false;
    return true;
}

static bool ensure_track(parser *p, int line, int col)
{
    /* Notas antes de qualquer "faixa" vão para uma faixa padrão. */
    return p->track >= 0 || start_track(p, "principal", line, col);
}

/* Marca o começo da música da faixa, para onde volta um :| sem |:. */
static void mark_started(parser *p, cursor at)
{
    if (!p->started) {
        p->started = true;
        p->track_start = at;
        p->last_repeat_end = at;
    }
}

/* ---------------------------------------------------------------------------
 * Elementos musicais
 * ------------------------------------------------------------------------ */

static bool same_keys(const parser *p, const int *keys, int n)
{
    if (n != p->tie_count) return false;
    for (int k = 0; k < n; k++) {
        bool found = false;
        for (int j = 0; j < n; j++) {
            if (p->s->events[p->tie_first + j].key == keys[k]) found = true;
        }
        if (!found) return false;
    }
    return true;
}

static bool emit(parser *p, const int *keys, int n, long dur, bool tie, int line, int col)
{
    song *s = p->s;
    if (p->tie_open) {
        /* Ligadura: a nota anterior cresce em vez de uma nova começar. */
        if (!same_keys(p, keys, n)) {
            return fail(p, line, col, "a ligadura (~) precisa ligar a mesma nota (ou o mesmo acorde)");
        }
        for (int k = 0; k < n; k++) s->events[p->tie_first + k].dur += dur;
    } else {
        if (s->nevents + n > MAX_EVENTS) return fail(p, line, col, "notas demais");
        note_event *grown = realloc(s->events, (size_t)(s->nevents + n) * sizeof *grown);
        if (!grown) return fail(p, line, col, "memória insuficiente");
        s->events = grown;
        for (int k = 0; k < n; k++) {
            note_event *e = &s->events[s->nevents++];
            e->track = p->track;
            e->start = p->time;
            e->dur = dur;
            e->key = keys[k];
            e->velocity = p->velocity;
        }
        s->tracks[p->track].nnotes += n;
        p->tie_first = s->nevents - n;
        p->tie_count = n;
        p->has_notes = true;
    }
    p->time += dur;
    p->tie_open = tie;
    if (tie) {
        p->tie_line = line;
        p->tie_col = col;
    }
    return true;
}

/* Depois da altura: ":duração" opcional e "~" opcional. */
static bool parse_dur_tie(parser *p, long *dur, bool *tie)
{
    *dur = p->last_dur;
    *tie = false;
    if (peek(p) == ':' && peek2(p) != '|') {
        advance(p);
        if (!parse_duration_at(p, dur)) return false;
        p->last_dur = *dur;
    }
    if (peek(p) == '~') {
        advance(p);
        *tie = true;
    }
    return true;
}

static bool key_for_word(parser *p, const char *w, int line, int col, int *key)
{
    if (p->s->tracks[p->track].instrument == INSTR_BATERIA) {
        *key = drum_index(w);
        if (*key < 0) {
            return fail(p, line, col,
                        "\"%s\" não é um tambor: use bumbo, caixa, chimbal, chimbal_aberto ou prato", w);
        }
        return true;
    }
    if (parse_pitch_word(w, key) == 0) return true;
    if (drum_index(w) >= 0) {
        return fail(p, line, col, "\"%s\" só vale numa faixa com instrumento bateria", w);
    }
    return fail(p, line, col, "nota inválida \"%s\": use C4, F#3, Bb5 ou dó4, fá#3, sib5", w);
}

static bool note(parser *p, const char *w, int line, int col)
{
    int key;
    long dur;
    bool tie;
    if (!key_for_word(p, w, line, col, &key) || !parse_dur_tie(p, &dur, &tie)) return false;
    return emit(p, &key, 1, dur, tie, line, col);
}

static bool rest(parser *p, int line, int col)
{
    long dur;
    bool tie;
    if (!parse_dur_tie(p, &dur, &tie)) return false;
    if (tie) return fail(p, line, col, "pausa não tem ligadura");
    if (p->tie_open) return fail(p, line, col, "a ligadura (~) precisa de uma nota em seguida, não de uma pausa");
    p->time += dur;
    return true;
}

static bool chord(parser *p, int line, int col)
{
    int keys[MAX_CHORD], n = 0;
    advance(p); /* [ */
    for (;;) {
        skip_blank(p);
        if (peek(p) == ']') {
            advance(p);
            break;
        }
        if (peek(p) == '\0') return fail(p, line, col, "acorde sem o ] de fechamento");
        int wl = p->c.line, wc = column(p, p->c.pos);
        char w[MAX_WORD];
        if (!is_word_char(peek(p))) return fail(p, wl, wc, "caractere inesperado dentro do acorde");
        if (!read_word(p, w)) return false;
        int key;
        if (!key_for_word(p, w, wl, wc, &key)) return false;
        for (int k = 0; k < n; k++) {
            if (keys[k] == key) return fail(p, wl, wc, "nota repetida no acorde");
        }
        if (n == MAX_CHORD) return fail(p, wl, wc, "acorde com mais de %d notas", MAX_CHORD);
        keys[n++] = key;
    }
    if (n == 0) return fail(p, line, col, "acorde vazio");
    long dur;
    bool tie;
    if (!parse_dur_tie(p, &dur, &tie)) return false;
    return emit(p, keys, n, dur, tie, line, col);
}

/* Barra de compasso: a duração desde o início (descontada a anacruse) tem de
 * ser um número inteiro de compassos. */
static bool barline(parser *p, int line, int col)
{
    char a[32], b[32];
    if (!p->bar_seen) {
        if (p->time > p->measure) {
            return fail(p, line, col, "o primeiro compasso tem duração %s, mais que um compasso de %d/%d",
                        fmt_dur(p->time, a, sizeof a), p->s->beats, p->s->unit);
        }
        p->bar_offset = p->time % p->measure; /* anacruse: o primeiro compasso pode ser menor */
        p->bar_seen = true;
    } else if ((p->time - p->bar_offset) % p->measure != 0) {
        return fail(p, line, col, "este compasso tem duração %s, mas o compasso %d/%d pede %s",
                    fmt_dur(p->time - p->last_bar, a, sizeof a), p->s->beats, p->s->unit,
                    fmt_dur(p->measure, b, sizeof b));
    }
    p->last_bar = p->time;
    return true;
}

/* Sinais de repetição também são barras de compasso, mas podem cair no meio
 * de um compasso (por exemplo, quando a repetição começa numa anacruse).
 * Por isso só entram na grade de compassos quando estão alinhados. */
static void repeat_bar(parser *p)
{
    if (!p->bar_seen) {
        if (p->time > 0 && p->time <= p->measure) {
            p->bar_offset = p->time % p->measure;
            p->bar_seen = true;
            p->last_bar = p->time;
        }
    } else if ((p->time - p->bar_offset) % p->measure == 0) {
        p->last_bar = p->time;
    }
}

static bool repeat_start(parser *p, int line, int col)
{
    if (p->rep_open) return fail(p, line, col, "repetição dentro de outra repetição não é suportada");
    repeat_bar(p);
    p->rep_open = true;
    p->rep_second = false;
    p->rep_start = p->c;
    p->rep_line = line;
    p->rep_col = col;
    return true;
}

static bool repeat_end(parser *p, int line, int col)
{
    repeat_bar(p);
    if (!p->rep_second) {
        if (!p->rep_open) {
            /* :| sem |: repete desde o fim da repetição anterior (ou o começo). */
            p->rep_open = true;
            p->rep_start = p->last_repeat_end;
            p->rep_line = line;
            p->rep_col = col;
        }
        p->rep_second = true;
        p->c = p->rep_start;
    } else {
        p->rep_open = false;
        p->rep_second = false;
        p->last_repeat_end = p->c;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * Diretivas
 * ------------------------------------------------------------------------ */

static bool directive(parser *p, const char *w, int line, int col, bool *handled)
{
    long v, v2;
    *handled = true;
    if (strcmp(w, "tempo") == 0 || strcmp(w, "compasso") == 0) {
        if (p->header_closed) {
            return fail(p, line, col, "%s vai no começo da partitura, antes das faixas e das notas", w);
        }
        if (w[0] == 't') {
            if (!read_int(p, "o tempo", 20, 400, &v)) return false;
            p->s->tempo = (int)v;
        } else {
            if (!read_int(p, "o número de tempos do compasso", 1, 32, &v)) return false;
            int sl = p->c.line, sc = column(p, p->c.pos);
            if (peek(p) != '/') return fail(p, sl, sc, "use o compasso na forma 3/4");
            advance(p);
            if (!read_int(p, "a unidade do compasso", 1, 64, &v2)) return false;
            if ((v2 & (v2 - 1)) != 0) return fail(p, sl, sc, "a unidade do compasso deve ser 1, 2, 4, 8, 16, 32 ou 64");
            p->s->beats = (int)v;
            p->s->unit = (int)v2;
        }
        return true;
    }
    if (strcmp(w, "faixa") == 0) {
        skip_inline_space(p);
        int nl = p->c.line, nc = column(p, p->c.pos);
        char name[MAX_WORD];
        if (!is_word_char(peek(p)) || !read_word(p, name)) {
            return p->failed ? false : fail(p, nl, nc, "faltou o nome da faixa");
        }
        return start_track(p, name, line, col);
    }
    if (strcmp(w, "instrumento") == 0) {
        if (!ensure_track(p, line, col)) return false;
        skip_inline_space(p);
        int nl = p->c.line, nc = column(p, p->c.pos);
        char name[MAX_WORD];
        if (!is_word_char(peek(p)) || !read_word(p, name)) {
            return p->failed ? false : fail(p, nl, nc, "faltou o nome do instrumento");
        }
        if (p->has_notes) return fail(p, line, col, "o instrumento vem antes das notas da faixa");
        /* "órgão" com acento também vale. */
        if (strcmp(name, "\xC3\xB3rg\xC3\xA3o") == 0) strcpy(name, "orgao");
        for (int k = 0; k < INSTR_COUNT; k++) {
            if (strcmp(name, INSTRUMENTS[k]) == 0) {
                track_info *t = &p->s->tracks[p->track];
                t->instrument = (instrument_id)k;
                if (!p->reverb_set) t->reverb = DEFAULT_REVERB[k];
                return true;
            }
        }
        return fail(p, nl, nc,
                    "instrumento desconhecido \"%s\": use piano, orgao, baixo, flauta, chiptune, sino ou bateria",
                    name);
    }
    if (strcmp(w, "volume") == 0 || strcmp(w, "pan") == 0 || strcmp(w, "reverb") == 0) {
        if (!ensure_track(p, line, col)) return false;
        if (p->has_notes) return fail(p, line, col, "%s vem antes das notas da faixa", w);
        track_info *t = &p->s->tracks[p->track];
        if (w[0] == 'v') {
            if (!read_int(p, "o volume", 0, 100, &v)) return false;
            t->volume = (int)v;
        } else if (w[0] == 'p') {
            if (!read_int(p, "o pan", -100, 100, &v)) return false;
            t->pan = (int)v;
        } else {
            if (!read_int(p, "o reverb", 0, 100, &v)) return false;
            t->reverb = (int)v;
            p->reverb_set = true;
        }
        return true;
    }
    for (size_t k = 0; k < sizeof DYNAMICS / sizeof DYNAMICS[0]; k++) {
        if (strcmp(w, DYNAMICS[k].name) == 0) {
            if (!ensure_track(p, line, col)) return false;
            p->velocity = DYNAMICS[k].velocity;
            return true;
        }
    }
    *handled = false;
    return true;
}

/* ---------------------------------------------------------------------------
 * Laço principal
 * ------------------------------------------------------------------------ */

int song_parse(const char *src, const char *file, song *s, char *err, size_t errcap)
{
    memset(s, 0, sizeof *s);
    s->tempo = 120;
    s->beats = 4;
    s->unit = 4;

    parser p;
    memset(&p, 0, sizeof p);
    p.src = src;
    p.c.line = 1;
    p.file = file ? file : "<partitura>";
    p.s = s;
    p.err = err;
    p.errcap = errcap;
    p.track = -1;
    if (errcap) err[0] = '\0';

    while (!p.failed) {
        skip_blank(&p);
        int c = peek(&p);
        if (c == '\0') break;
        cursor at = p.c;
        int line = p.c.line, col = column(&p, p.c.pos);

        if (c == '[') {
            if (!ensure_track(&p, line, col)) break;
            mark_started(&p, at);
            chord(&p, line, col);
        } else if (c == '|') {
            if (!ensure_track(&p, line, col)) break;
            mark_started(&p, at);
            advance(&p);
            if (peek(&p) == ':') {
                advance(&p);
                repeat_start(&p, line, col);
            } else {
                barline(&p, line, col);
            }
        } else if (c == ':' && peek2(&p) == '|') {
            if (!ensure_track(&p, line, col)) break;
            mark_started(&p, at);
            advance(&p);
            advance(&p);
            repeat_end(&p, line, col);
        } else if (is_word_char(c)) {
            char w[MAX_WORD];
            bool handled;
            if (!read_word(&p, w)) break;
            if (!directive(&p, w, line, col, &handled)) break;
            if (handled) continue;
            if (!ensure_track(&p, line, col)) break;
            mark_started(&p, at);
            if (strcmp(w, "r") == 0 || strcmp(w, "R") == 0) {
                rest(&p, line, col);
            } else {
                note(&p, w, line, col);
            }
        } else if (c >= 0x20 && c < 0x7f) {
            fail(&p, line, col, "caractere inesperado '%c'", c);
        } else {
            fail(&p, line, col, "caractere inesperado (byte 0x%02X)", c);
        }
    }

    if (!p.failed) finish_track(&p);
    if (!p.failed && s->nevents == 0) fail(&p, p.c.line, column(&p, p.c.pos), "a partitura não tem nenhuma nota");
    if (!p.failed && song_seconds(s, song_length(s)) > MAX_SECONDS) {
        fail(&p, p.c.line, column(&p, p.c.pos), "a música passa de 30 minutos");
    }
    if (p.failed) {
        song_free(s);
        return -1;
    }
    return 0;
}
