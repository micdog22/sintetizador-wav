#!/bin/sh
# Testes de integração: gera os exemplos e confere os WAVs com ferramentas de
# fora do projeto (o módulo wave do Python e, no macOS, o afinfo).
# Uso: sh tests/wav.sh <pasta do build>

B=${1:-build}
DIR=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/sintetizador.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT INT TERM

total=0
falhas=0
ok() { total=$((total + 1)); }
falha() { total=$((total + 1)); falhas=$((falhas + 1)); echo "FALHOU: $*"; }

# Gera todos os exemplos.
for p in "$DIR"/exemplos/*.txt; do
    nome=$(basename "$p" .txt)
    if "$B/sintetizador" "$p" -o "$TMP/$nome.wav" > "$TMP/$nome.log"; then ok; else falha "gerar $nome"; fi
done

# O módulo wave do Python lê o cabeçalho e as amostras.
if command -v python3 > /dev/null 2>&1; then
    python3 - "$TMP"/*.wav > "$TMP/python.txt" 2>&1 <<'EOF'
import array, sys, wave
bad = 0
for path in sys.argv[1:]:
    with wave.open(path, "rb") as w:
        ch, width, rate, frames, comp = (w.getnchannels(), w.getsampwidth(), w.getframerate(),
                                         w.getnframes(), w.getcomptype())
        data = array.array("h", w.readframes(frames))
    if sys.byteorder == "big":
        data.byteswap()
    peak = max(abs(x) for x in data)
    problems = []
    if (ch, width, rate, comp) != (2, 2, 44100, "NONE"):
        problems.append(f"formato {ch} canais, {width} bytes, {rate} Hz, {comp}")
    if frames < 10 * rate:
        problems.append(f"curto demais: {frames} quadros")
    if len(data) != 2 * frames:
        problems.append("quantidade de amostras não bate com o cabeçalho")
    if not 1000 < peak <= 32767:
        problems.append(f"pico estranho: {peak}")
    print(path.rsplit("/", 1)[-1], "OK" if not problems else "; ".join(problems))
    bad += bool(problems)
sys.exit(1 if bad else 0)
EOF
    if [ $? -eq 0 ]; then ok; else falha "módulo wave do Python"; cat "$TMP/python.txt"; fi
else
    echo "python3 não encontrado: pulando a conferência com o módulo wave"
fi

# afinfo (macOS) usa o leitor de áudio do sistema.
if command -v afinfo > /dev/null 2>&1; then
    for w in "$TMP"/*.wav; do
        if afinfo "$w" | grep -Eq "2 ch, +44100 Hz, Int16"; then ok; else falha "afinfo $(basename "$w")"; fi
    done
else
    echo "afinfo não encontrado (só existe no macOS): pulando"
fi

# A mesma partitura gera sempre o mesmo áudio.
h1=$("$B/sintetizador" "$DIR/exemplos/fase-bonus.txt" -o "$TMP/a.wav" --hash | sed -n 's/^hash: //p')
h2=$("$B/sintetizador" "$DIR/exemplos/fase-bonus.txt" -o "$TMP/b.wav" --hash | sed -n 's/^hash: //p')
if [ -n "$h1" ] && [ "$h1" = "$h2" ] && cmp -s "$TMP/a.wav" "$TMP/b.wav"; then ok; else falha "determinismo"; fi

# Erro na partitura: código 1 e mensagem com arquivo, linha e coluna.
printf 'tempo 100\nC4 X9 E4\n' > "$TMP/ruim.txt"
"$B/sintetizador" "$TMP/ruim.txt" -o "$TMP/ruim.wav" 2> "$TMP/erro.txt"
rc=$?
if [ $rc -eq 1 ] && grep -q 'ruim.txt:2:4: erro: nota inválida "X9"' "$TMP/erro.txt"; then ok; else falha "erro na partitura (código $rc)"; fi

# --info não gera áudio e lista as faixas.
"$B/sintetizador" --info "$DIR/exemplos/ode-a-alegria.txt" > "$TMP/info.txt"
if grep -q "compasso 4/4" "$TMP/info.txt" && grep -q "faixa melodia" "$TMP/info.txt"; then ok; else falha "--info"; fi

echo "wav.sh: $total verificações, $falhas falhas"
[ "$falhas" -eq 0 ]
