/*
 * test_partitura.c: alturas, durações, acordes, ligaduras, repetições,
 * dinâmica, conferência de compassos e mensagens de erro com posição.
 */
#include <stdlib.h>

#include "partitura.h"
#include "teste.h"

static song s;
static char err[512];

static int pitch(const char *w)
{
    int m;
    return parse_pitch_word(w, &m) == 0 ? m : -1;
}

static long dur(const char *t)
{
    long ticks;
    return parse_duration_text(t, &ticks) == 0 ? ticks : -1;
}

static void test_pitches(void)
{
    CHECK_EQ(pitch("C4"), 60);   /* dó central */
    CHECK_EQ(pitch("A4"), 69);   /* lá de 440 Hz */
    CHECK_EQ(pitch("a4"), 69);
    CHECK_EQ(pitch("C#4"), 61);
    CHECK_EQ(pitch("Db4"), 61);  /* enarmônicos */
    CHECK_EQ(pitch("Cb4"), 59);
    CHECK_EQ(pitch("B#3"), 60);
    CHECK_EQ(pitch("C##4"), 62);
    CHECK_EQ(pitch("Ebb4"), 62);
    CHECK_EQ(pitch("Bb3"), 58);
    CHECK_EQ(pitch("bb3"), 58);  /* a primeira letra é a nota, o b seguinte é bemol */
    CHECK_EQ(pitch("C0"), 12);
    CHECK_EQ(pitch("G9"), 127);
    CHECK_EQ(pitch("G#9"), -1);  /* passa de 127 */
    /* Solfejo, com e sem acento. */
    CHECK_EQ(pitch("dó4"), 60);
    CHECK_EQ(pitch("do4"), 60);
    CHECK_EQ(pitch("Dó4"), 60);
    CHECK_EQ(pitch("ré4"), 62);
    CHECK_EQ(pitch("re#4"), 63);
    CHECK_EQ(pitch("mib4"), 63);
    CHECK_EQ(pitch("fá4"), 65);
    CHECK_EQ(pitch("fa#4"), 66);
    CHECK_EQ(pitch("sol4"), 67);
    CHECK_EQ(pitch("SOL4"), 67);
    CHECK_EQ(pitch("lá4"), 69);
    CHECK_EQ(pitch("sib3"), 58);
    CHECK_EQ(pitch("si4"), 71);
    /* Inválidas. */
    CHECK_EQ(pitch("H4"), -1);
    CHECK_EQ(pitch("C"), -1);
    CHECK_EQ(pitch("C44"), -1);
    CHECK_EQ(pitch("C#b4"), -1);
    CHECK_EQ(pitch("C###4"), -1);
    CHECK_EQ(pitch("sola4"), -1);
    CHECK_EQ(pitch(""), -1);
}

static void test_durations(void)
{
    CHECK_EQ(dur("1"), 768);        /* semibreve */
    CHECK_EQ(dur("1/2"), 384);
    CHECK_EQ(dur("1/4"), 192);      /* semínima */
    CHECK_EQ(dur("1/8"), 96);
    CHECK_EQ(dur("1/64"), 12);
    CHECK_EQ(dur("2"), 1536);
    CHECK_EQ(dur("3/8"), 288);
    CHECK_EQ(dur("1/4."), 288);     /* pontuada: 1/4 + 1/8 */
    CHECK_EQ(dur("1/8."), 144);
    CHECK_EQ(dur("1/4.."), 336);    /* duplamente pontuada: 1/4 + 1/8 + 1/16 */
    CHECK_EQ(dur("1/8t"), 64);      /* três colcheias de quiáltera = uma semínima */
    CHECK_EQ(dur("1/4t"), 128);
    CHECK_EQ(dur("1/64..t"), 14);   /* o caso mais difícil continua inteiro */
    CHECK_EQ(3 * dur("1/8t"), dur("1/4"));
    CHECK_EQ(dur("1/3"), -1);
    CHECK_EQ(dur("0/4"), -1);
    CHECK_EQ(dur("1/128"), -1);
    CHECK_EQ(dur("65"), -1);
    CHECK_EQ(dur("1/4..."), -1);
    CHECK_EQ(dur("1/4x"), -1);
    CHECK_EQ(dur("/4"), -1);
    CHECK_EQ(dur("1/"), -1);
    CHECK_EQ(dur(""), -1);
}

static int parse(const char *src)
{
    return song_parse(src, "t.txt", &s, err, sizeof err);
}

/* A partitura tem de falhar com a mensagem (e a posição) esperada. */
static void expect_error(const char *src, const char *fragment)
{
    int rc = parse(src);
    CHECK_EQ(rc, -1);
    if (rc == 0) {
        song_free(&s);
        fprintf(stderr, "  esperava erro em: %s\n", src);
        return;
    }
    if (!strstr(err, fragment)) {
        fprintf(stderr, "  esperava \"%s\" em: %s\n", fragment, err);
        CHECK(0);
    } else {
        CHECK(1);
    }
}

static void test_basic_song(void)
{
    const char *src =
        "# comentário no começo\n"
        "tempo 90\n"
        "compasso 3/4\n"
        "faixa melodia   # comentário no fim da linha\n"
        "instrumento flauta\n"
        "volume 70\n"
        "pan -30\n"
        "C4:1/4 D4 E4 | [C4 E4 G4]:1/2. | r:1/4 F#4:1/8 G4 A4:1/4 |\n"
        "faixa ritmo\n"
        "instrumento bateria\n"
        "bumbo:1/4 [caixa chimbal] chimbal_aberto | prato:1/2. |\n";
    CHECK_EQ(parse(src), 0);
    if (s.nevents == 0) return;
    CHECK_EQ(s.tempo, 90);
    CHECK_EQ(s.beats, 3);
    CHECK_EQ(s.unit, 4);
    CHECK_EQ(s.ntracks, 2);
    CHECK_STR(s.tracks[0].name, "melodia");
    CHECK_EQ(s.tracks[0].instrument, INSTR_FLAUTA);
    CHECK_EQ(s.tracks[0].volume, 70);
    CHECK_EQ(s.tracks[0].pan, -30);
    CHECK_EQ(s.tracks[0].reverb, 25);           /* padrão da flauta */
    CHECK_EQ(s.tracks[0].nnotes, 9);
    CHECK_EQ(s.tracks[0].length, 3 * 576);     /* três compassos de 3/4 */
    /* D4 herda a duração do C4 anterior. */
    CHECK_EQ(s.events[1].key, 62);
    CHECK_EQ(s.events[1].start, 192);
    CHECK_EQ(s.events[1].dur, 192);
    /* Acorde: três eventos que começam juntos. */
    CHECK_EQ(s.events[3].start, 576);
    CHECK_EQ(s.events[4].start, 576);
    CHECK_EQ(s.events[5].start, 576);
    CHECK_EQ(s.events[5].key, 67);
    CHECK_EQ(s.events[5].dur, 576);
    /* A pausa empurra o F#4 para depois de uma semínima. */
    CHECK_EQ(s.events[6].key, 66);
    CHECK_EQ(s.events[6].start, 2 * 576 + 192);
    CHECK_EQ(s.events[7].dur, 96);              /* G4 herda a colcheia do F#4 */
    CHECK_EQ(s.events[8].dur, 192);
    /* Bateria: os nomes viram tambores. */
    CHECK_EQ(s.tracks[1].instrument, INSTR_BATERIA);
    CHECK_EQ(s.tracks[1].length, 2 * 576);
    CHECK_EQ(s.events[9].key, DRUM_BUMBO);
    CHECK_EQ(s.events[10].key, DRUM_CAIXA);
    CHECK_EQ(s.events[11].key, DRUM_CHIMBAL);
    CHECK_EQ(s.events[11].start, 192);
    CHECK_EQ(s.events[12].key, DRUM_CHIMBAL_ABERTO);
    CHECK_EQ(s.events[13].key, DRUM_PRATO);
    CHECK_EQ(song_length(&s), 3 * 576);
    /* 3 compassos de 3/4 a 90 semínimas por minuto = 9 semínimas = 6 s. */
    CHECK(song_seconds(&s, song_length(&s)) > 5.999 && song_seconds(&s, song_length(&s)) < 6.001);
    song_free(&s);
}

static void test_dynamics_ties_repeats(void)
{
    /* Dinâmica: cada marca vale para as notas seguintes. */
    CHECK_EQ(parse("C4 pp D4 f E4 ff F4 mp G4 p A4 mf B4"), 0);
    CHECK(s.events[0].velocity == 0.75);       /* padrão: mf */
    CHECK(s.events[1].velocity == 0.30);
    CHECK(s.events[2].velocity == 0.90);
    CHECK(s.events[3].velocity == 1.00);
    CHECK(s.events[4].velocity == 0.60);
    CHECK(s.events[5].velocity == 0.45);
    CHECK(s.events[6].velocity == 0.75);
    CHECK_STR(s.tracks[0].name, "principal");  /* faixa criada sozinha */
    song_free(&s);

    /* Ligadura: vira uma nota só, inclusive através da barra de compasso. */
    CHECK_EQ(parse("compasso 2/4\nC4:1/4 D4:1/4~ | D4:1/8 E4 F4:1/4"), 0);
    CHECK_EQ(s.nevents, 4);
    CHECK_EQ(s.events[1].key, 62);
    CHECK_EQ(s.events[1].dur, 192 + 96);
    CHECK_EQ(s.events[2].start, 384 + 96);
    song_free(&s);

    /* Acorde ligado a acorde igual (em outra ordem). */
    CHECK_EQ(parse("[C4 E4]:1/2~ [E4 C4]:1/4 G4"), 0);
    CHECK_EQ(s.nevents, 3);
    CHECK_EQ(s.events[0].dur, 576);
    CHECK_EQ(s.events[1].dur, 576);
    CHECK_EQ(s.events[2].start, 576);
    song_free(&s);

    /* Repetição: o trecho entre |: e :| toca duas vezes. */
    CHECK_EQ(parse("compasso 2/4\nC4:1/4 C4 |: D4 E4 | F4 G4 :| A4:1/2 |"), 0);
    CHECK_EQ(s.nevents, 11);
    static const int keys[11] = { 60, 60, 62, 64, 65, 67, 62, 64, 65, 67, 69 };
    for (int k = 0; k < 11; k++) {
        CHECK_EQ(s.events[k].key, keys[k]);
        CHECK_EQ(s.events[k].start, k * 192);
    }
    CHECK_EQ(s.tracks[0].length, 12 * 192);
    song_free(&s);

    /* :| sem |: repete desde o começo; o seguinte, desde o último :|. */
    CHECK_EQ(parse("C4 D4 :| E4 :|"), 0);
    static const int keys2[6] = { 60, 62, 60, 62, 64, 64 };
    CHECK_EQ(s.nevents, 6);
    for (int k = 0; k < 6 && k < s.nevents; k++) CHECK_EQ(s.events[k].key, keys2[k]);
    song_free(&s);

    /* Repetição que começa na anacruse: o último compasso dela tem 3/8, o :|
     * cai no meio do compasso e a colcheia que falta vem depois. */
    CHECK_EQ(parse("compasso 2/4\n|: C4:1/8 | F4:1/4 F4 | G4:1/8 E4 F4 :| C4:1/8 | C4:1/2 |"), 0);
    CHECK_EQ(s.nevents, 14);
    song_free(&s);
    expect_error("compasso 2/4\n|: C4:1/8 | F4:1/4 F4 | G4:1/8 E4 F4 :| C4:1/2 |",
                 "t.txt:2:48: erro: este compasso tem duração 7/8");

    /* Anacruse: o primeiro compasso pode ser incompleto. */
    CHECK_EQ(parse("compasso 2/4\nC4:1/8 | F4:1/4 F4 | G4:1/2 |"), 0);
    song_free(&s);

    /* # dentro de uma palavra é sustenido; no começo, comentário. */
    CHECK_EQ(parse("C#4 #D4 E4\nF#4"), 0);
    CHECK_EQ(s.nevents, 2);
    CHECK_EQ(s.events[1].key, 66);
    song_free(&s);

    /* O nome do órgão aceita acento. */
    CHECK_EQ(parse("instrumento órgão\nC4"), 0);
    CHECK_EQ(s.tracks[0].instrument, INSTR_ORGAO);
    song_free(&s);

    /* Quiálteras: três colcheias de tercina ocupam uma semínima. */
    CHECK_EQ(parse("compasso 1/4\nC4:1/8t D4 E4 | F4:1/4 |"), 0);
    CHECK_EQ(s.events[3].start, 192);
    song_free(&s);
}

static void test_errors(void)
{
    expect_error("C4 H4", "t.txt:1:4: erro: nota inválida \"H4\"");
    expect_error("C4:1/3", "t.txt:1:4: erro: o denominador da duração");
    expect_error("\n  C4:1/4...", "t.txt:2:6: erro: no máximo dois pontos");
    expect_error("instrumento violino", "t.txt:1:13: erro: instrumento desconhecido \"violino\"");
    expect_error("tempo 500", "t.txt:1:7: erro: o tempo deve ficar entre 20 e 400");
    expect_error("C4\ntempo 100", "t.txt:2:1: erro: tempo vai no começo");
    expect_error("compasso 3/5", "a unidade do compasso deve ser");
    expect_error("[C4 E4", "t.txt:1:1: erro: acorde sem o ] de fechamento");
    expect_error("[]:1/4", "acorde vazio");
    expect_error("[C4 C4]", "t.txt:1:5: erro: nota repetida no acorde");
    expect_error("C4~ D4", "t.txt:1:5: erro: a ligadura (~) precisa ligar a mesma nota");
    expect_error("C4~ r", "a ligadura (~) precisa de uma nota em seguida");
    expect_error("C4 D4~", "t.txt:1:4: erro: a ligadura (~) desta nota não tem nota seguinte");
    expect_error("|: C4 |: D4 :|", "t.txt:1:7: erro: repetição dentro de outra");
    expect_error("C4 |: D4", "t.txt:1:4: erro: a repetição aberta aqui não foi fechada");
    expect_error("compasso 2/4\nC4:1/4 D4 | E4 |", "t.txt:2:16: erro: este compasso tem duração 1/4, mas o compasso 2/4 pede 1/2");
    expect_error("compasso 2/4\nC4:1/2 D4:1/8 |", "o primeiro compasso tem duração 5/8");
    expect_error("C4 bumbo", "\"bumbo\" só vale numa faixa com instrumento bateria");
    expect_error("instrumento bateria\nC4", "t.txt:2:1: erro: \"C4\" não é um tambor");
    expect_error("C4\ninstrumento piano", "t.txt:2:1: erro: o instrumento vem antes das notas");
    expect_error("C4 pan 10", "pan vem antes das notas");
    expect_error("pan 101", "o pan deve ficar entre -100 e 100");
    expect_error("faixa", "faltou o nome da faixa");
    expect_error("C4 @", "t.txt:1:4: erro: caractere inesperado '@'");
    expect_error("# só comentário\n", "a partitura não tem nenhuma nota");
    expect_error("r:1 r:1", "a partitura não tem nenhuma nota");
    expect_error("ré4 é4", "t.txt:1:5: erro: nota inválida");  /* coluna conta caracteres, não bytes */
    expect_error("tempo 20\nC4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 "
                 "C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 "
                 "C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64 C4:64",
                 "a música passa de 30 minutos");
}

int main(void)
{
    test_pitches();
    test_durations();
    test_basic_song();
    test_dynamics_ties_repeats();
    test_errors();
    return teste_resumo("test_partitura");
}
