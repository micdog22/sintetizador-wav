/*
 * wav.c: escrita do cabeçalho RIFF/WAVE e das amostras.
 */
#include "wav.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)(v >> 24);
}

void wav_header(uint8_t out[WAV_HEADER_SIZE], uint32_t frames, uint16_t channels, uint32_t rate)
{
    uint16_t bits = 16;
    uint16_t block_align = (uint16_t)(channels * bits / 8);   /* bytes por quadro */
    uint32_t data_size = frames * block_align;
    memcpy(out, "RIFF", 4);
    put32(out + 4, 36 + data_size);     /* tamanho do resto do arquivo */
    memcpy(out + 8, "WAVE", 4);
    memcpy(out + 12, "fmt ", 4);
    put32(out + 16, 16);                /* tamanho do bloco fmt */
    put16(out + 20, 1);                 /* 1 = PCM sem compressão */
    put16(out + 22, channels);
    put32(out + 24, rate);
    put32(out + 28, rate * block_align); /* bytes por segundo */
    put16(out + 32, block_align);
    put16(out + 34, bits);
    memcpy(out + 36, "data", 4);
    put32(out + 40, data_size);
}

void wav_pack(const int16_t *samples, size_t count, uint8_t *out)
{
    for (size_t i = 0; i < count; i++) {
        put16(out + 2 * i, (uint16_t)samples[i]); /* complemento de dois, byte baixo primeiro */
    }
}
