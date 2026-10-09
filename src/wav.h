/*
 * wav.h: o formato WAV (RIFF) com áudio PCM de 16 bits.
 *
 * Os números do arquivo são little-endian. Em vez de copiar structs para o
 * disco (o que dependeria da ordem de bytes e do alinhamento da máquina),
 * cada campo é escrito byte a byte.
 */
#ifndef WAV_H
#define WAV_H

#include <stddef.h>
#include <stdint.h>

#define WAV_HEADER_SIZE 44

/* Cabeçalho de 44 bytes para "frames" quadros de 16 bits. */
void wav_header(uint8_t out[WAV_HEADER_SIZE], uint32_t frames, uint16_t channels, uint32_t rate);

/* Converte amostras para bytes little-endian (2 bytes por amostra). */
void wav_pack(const int16_t *samples, size_t count, uint8_t *out);

/* Maior número de quadros estéreo que cabe num WAV (o tamanho é de 32 bits). */
#define WAV_MAX_FRAMES ((0xFFFFFFFFu - 36u) / 4u)

#endif
