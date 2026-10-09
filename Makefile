# Makefile do sintetizador WAV.
# Uso: make, make test, make clean. CC, CFLAGS e LDFLAGS podem ser trocados:
#   make test CFLAGS="-O1 -g -fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined"
#
# -ffp-contract=off impede o compilador de fundir a*b+c numa instrução FMA.
# Sem isso o último bit das contas pode mudar de uma máquina para outra, e o
# mesmo arquivo de partitura geraria WAVs diferentes.

CFLAGS  ?= -O2 -g
LDFLAGS ?=
LDLIBS  := -lm
WARN    := -std=c11 -Wall -Wextra -Wpedantic
ALL_CFLAGS = $(WARN) -ffp-contract=off -Isrc $(CFLAGS)

B := build
HEADERS := $(wildcard src/*.h)
CORE := $(B)/dsp.o $(B)/partitura.o $(B)/instrumentos.o $(B)/render.o $(B)/wav.o
TESTES := $(B)/test_dsp $(B)/test_partitura $(B)/test_render

.PHONY: all test clean

all: $(B)/sintetizador

$(B):
	mkdir -p $@

$(B)/%.o: src/%.c $(HEADERS) | $(B)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(B)/sintetizador: $(B)/main.o $(CORE)
	$(CC) $(ALL_CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(B)/test_%: tests/test_%.c tests/teste.h $(CORE)
	$(CC) $(ALL_CFLAGS) $< $(CORE) $(LDFLAGS) $(LDLIBS) -o $@

test: all $(TESTES)
	$(B)/test_dsp
	$(B)/test_partitura
	$(B)/test_render
	sh tests/wav.sh $(B)

clean:
	rm -rf $(B)
