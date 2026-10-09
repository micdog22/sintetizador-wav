/*
 * render.c: agenda as vozes, mistura, aplica a reverberação e o limitador.
 */
#include "render.h"

#include <stdlib.h>
#include <string.h>

#define BLOCK 512
#define REVERB_TAIL_S 1.6
#define FADE_S 0.1
#define MASTER_GAIN 0.9

long ticks_to_samples(const song *s, long ticks)
{
    /* amostras = ticks / 192 semínimas * 60 / tempo segundos * 44100 */
    long long num = (long long)ticks * 60 * SAMPLE_RATE;
    long long den = (long long)s->tempo * TICKS_QUARTER;
    return (long)((num + den / 2) / den);
}

static int cmp_notes(const void *a, const void *b)
{
    const scheduled_note *x = a, *y = b;
    if (x->start != y->start) return x->start < y->start ? -1 : 1;
    return x->event - y->event; /* desempate estável: mesma ordem sempre */
}

int renderer_init(renderer *r, const song *s, bool use_reverb)
{
    memset(r, 0, sizeof *r);
    r->s = s;
    r->use_reverb = use_reverb;
    r->order = malloc((size_t)(s->nevents ? s->nevents : 1) * sizeof *r->order);
    if (!r->order) return -1;
    long last = 0;
    for (int k = 0; k < s->nevents; k++) {
        const note_event *e = &s->events[k];
        scheduled_note *n = &r->order[k];
        n->start = ticks_to_samples(s, e->start);
        n->off = ticks_to_samples(s, e->start + e->dur);
        n->event = k;
        instrument_id instr = s->tracks[e->track].instrument;
        long end = (instr == INSTR_BATERIA ? n->start : n->off) + instrument_tail(instr, e->key);
        if (end > last) last = end;
    }
    r->nnotes = s->nevents;
    qsort(r->order, (size_t)r->nnotes, sizeof *r->order, cmp_notes);
    long song_end = ticks_to_samples(s, song_length(s));
    if (song_end > last) last = song_end;
    r->total = last + (long)((use_reverb ? REVERB_TAIL_S : FADE_S) * SAMPLE_RATE);
    if (use_reverb && reverb_init(&r->rev, 1.6, 0.25) != 0) {
        free(r->order);
        return -1;
    }
    return 0;
}

static int add_voice(renderer *r, const scheduled_note *n)
{
    if (r->nvoices == r->capvoices) {
        int cap = r->capvoices ? r->capvoices * 2 : 32;
        voice *grown = realloc(r->voices, (size_t)cap * sizeof *grown);
        if (!grown) return -1;
        r->voices = grown;
        r->capvoices = cap;
    }
    const note_event *e = &r->s->events[n->event];
    const track_info *t = &r->s->tracks[e->track];
    voice_init(&r->voices[r->nvoices++], t->instrument, e->key, e->velocity, n->start, n->off,
               t->volume / 100.0, t->pan / 100.0, t->reverb / 100.0);
    return 0;
}

size_t renderer_run(renderer *r, int16_t *out, size_t frames)
{
    size_t done = 0;
    while (done < frames && r->pos < r->total) {
        long n = r->total - r->pos;
        if (n > BLOCK) n = BLOCK;
        if ((long)(frames - done) < n) n = (long)(frames - done);
        long b0 = r->pos, b1 = r->pos + n;
        double left[BLOCK], right[BLOCK], send[BLOCK];
        memset(left, 0, sizeof left);
        memset(right, 0, sizeof right);
        memset(send, 0, sizeof send);

        /* Entram as vozes que começam neste bloco. */
        while (r->next < r->nnotes && r->order[r->next].start < b1) {
            if (add_voice(r, &r->order[r->next]) != 0) return done;
            r->next++;
        }
        /* Cada voz soma a sua parte do bloco; as que terminaram saem da lista. */
        int kept = 0;
        for (int k = 0; k < r->nvoices; k++) {
            voice *v = &r->voices[k];
            long from = v->start > b0 ? v->start : b0;
            long to = v->end < b1 ? v->end : b1;
            if (from < to) {
                voice_render(v, left + (from - b0), right + (from - b0), send + (from - b0), (int)(to - from));
            }
            if (v->end > b1) r->voices[kept++] = *v;
        }
        r->nvoices = kept;

        /* Reverberação, volume geral, desvanecimento final e limitador. */
        long fade_from = r->total - (long)(FADE_S * SAMPLE_RATE);
        for (long i = 0; i < n; i++) {
            double l = left[i], rr = right[i];
            if (r->use_reverb) {
                double wl, wr;
                reverb_run(&r->rev, send[i], &wl, &wr);
                l += wl;
                rr += wr;
            }
            l *= MASTER_GAIN;
            rr *= MASTER_GAIN;
            long abs_pos = b0 + i;
            if (abs_pos >= fade_from) {
                double g = (double)(r->total - abs_pos) / (double)(r->total - fade_from);
                l *= g;
                rr *= g;
            }
            double al = l < 0 ? -l : l, ar = rr < 0 ? -rr : rr;
            if (al > r->peak) r->peak = al;
            if (ar > r->peak) r->peak = ar;
            out[2 * (done + (size_t)i)] = to_int16(soft_clip(l));
            out[2 * (done + (size_t)i) + 1] = to_int16(soft_clip(rr));
        }
        r->pos = b1;
        done += (size_t)n;
    }
    return done;
}

void renderer_free(renderer *r)
{
    free(r->order);
    free(r->voices);
    if (r->use_reverb) reverb_free(&r->rev);
    memset(r, 0, sizeof *r);
}
