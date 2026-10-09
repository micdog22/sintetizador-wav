# Sintetizador WAV

Transforme partituras escritas em texto em música, do zero em C: um sintetizador com osciladores sem aliasing (PolyBLEP), envelopes ADSR, síntese FM, filtros, bateria sintetizada, reverberação de Schroeder e gravação em WAV. Sem nenhuma biblioteca de áudio, sem dependências: só a biblioteca padrão do C.

```
tempo 108
faixa melodia
instrumento flauta
E4:1/4 E4 F4 G4 | G4 F4 E4 D4 | C4 C4 D4 E4 | E4:1/4. D4:1/8 D4:1/2 |
```

## Recursos

- **Partitura em texto**: notas em letras (`C4`, `F#3`, `Bb5`) ou no solfejo (`dó4`, `fá#3`, `sib5`), durações com ponto e quiálteras, pausas, acordes, ligaduras, dinâmica (`pp` a `ff`), repetição `|: ... :|`, várias faixas e comentários.
- **Conferência de compassos**: cada barra `|` é conferida contra a fórmula de compasso, com anacruse. Os erros apontam arquivo, linha e coluna.
- **Sete instrumentos**: piano elétrico (FM), órgão (aditivo), baixo (subtrativo com filtro ressonante), flauta, chiptune (pulso de 25%), sino (FM inarmônica) e bateria (bumbo, caixa, chimbal, chimbal aberto e prato), todos sintetizados.
- **Osciladores** seno, quadrado com largura de pulso, dente de serra, triângulo e ruído, com anti-aliasing PolyBLEP.
- **Efeitos**: envelope ADSR por instrumento, filtros de um polo e biquad, pan estéreo de potência constante por faixa, reverberação de Schroeder (4 pentes e 2 passa-tudo) e limitador suave.
- **WAV PCM** de 44,1 kHz, 16 bits, estéreo, escrito em little-endian em qualquer máquina.
- **Determinístico**: a mesma partitura gera exatamente o mesmo arquivo em qualquer computador. Os testes conferem um hash.
- Exemplos em `exemplos/`: "Ode à Alegria" (Beethoven), "Brilha, Brilha, Estrelinha", "Ciranda, Cirandinha", "Atirei o Pau no Gato" (todas em domínio público) e "Fase Bônus", um chiptune original feito para o projeto.

## Como usar

```
$ make
$ ./build/sintetizador exemplos/ode-a-alegria.txt -o ode.wav
ode.wav: 0:39.4, 3 faixas, 164 notas, pico -3.2 dBFS
```

Abra o `ode.wav` em qualquer player. No macOS dá para ouvir direto do terminal com `afplay ode.wav`; no Linux, com `aplay ode.wav`.

```
$ ./build/sintetizador --info exemplos/ode-a-alegria.txt
tempo 108 (semínimas por minuto), compasso 4/4, duração 0:37.8
  faixa melodia      flauta      62 notas  volume  85  pan  -15  reverb  25
  faixa acordes      orgao       69 notas  volume  55  pan   20  reverb  20
  faixa baixo        baixo       33 notas  volume  70  pan    0  reverb   5
```

Errou a conta de um compasso? O sintetizador avisa onde:

```
$ cat valsa.txt
tempo 100
compasso 3/4
C4:1/4 D4 E4 | F4:1/2 |

$ ./build/sintetizador valsa.txt
valsa.txt:3:23: erro: este compasso tem duração 1/2, mas o compasso 3/4 pede 3/4
```

Opções:

```
uso: sintetizador [opções] partitura.txt

  -o, --saida ARQ   arquivo WAV de saída (padrão: o nome da partitura com .wav)
      --info        mostra tempo, compasso, faixas e duração, sem gerar áudio
      --hash        imprime o hash FNV-1a das amostras geradas
      --sem-reverb  desliga a reverberação
  -h, --help        mostra esta ajuda
  -v, --versao      mostra a versão
```

### O formato da partitura

**Cabeçalho.** `tempo` (semínimas por minuto, de 20 a 400; padrão 120) e `compasso` (como `3/4` ou `6/8`; padrão 4/4) vêm antes da primeira faixa.

**Faixas.** `faixa nome` começa uma faixa nova; todas tocam ao mesmo tempo, começando do zero. Depois dela, e antes das notas:

| Diretiva | Valores | Padrão |
|---|---|---|
| `instrumento` | `piano`, `orgao` (ou `órgão`), `baixo`, `flauta`, `chiptune`, `sino`, `bateria` | `piano` |
| `volume` | de 0 a 100 | 80 |
| `pan` | de -100 (esquerda) a 100 (direita) | 0 |
| `reverb` | de 0 a 100 (quanto do som vai para a reverberação) | depende do instrumento |

Notas escritas antes de qualquer `faixa` vão para uma faixa chamada `principal`.

**Notas.** Letra (`C D E F G A B`, maiúscula ou minúscula) ou nome do solfejo (`dó ré mi fá sol lá si`, com ou sem acento), seguida de acidentes (`#` sustenido, `b` bemol, até dois) e da oitava. `C4` é o dó central e `A4` é o lá de 440 Hz.

| Escrita | Nota MIDI | Frequência |
|---|---|---|
| `A4`, `lá4` | 69 | 440,00 Hz |
| `C4`, `dó4` | 60 | 261,63 Hz |
| `F#3`, `fá#3`, `Gb3` | 54 | 185,00 Hz |
| `Bb5`, `sib5` | 82 | 932,33 Hz |

**Durações.** Depois de dois-pontos, como fração da semibreve: `C4:1/4` é uma semínima, `C4:1/8` uma colcheia, `C4:1` uma semibreve. Um ponto aumenta metade (`1/4.` vale `1/4 + 1/8`), dois pontos aumentam três quartos (`1/4..`) e `t` faz quiáltera de três (`1/8t`: três delas valem uma semínima). Sem duração, a nota repete a duração da anterior:

```
C4:1/8 D4 E4 F4 G4:1/2      # quatro colcheias e uma mínima
```

**Pausas, acordes e ligaduras.**

```
r:1/4                       # pausa de semínima
[C4 E4 G4]:1/2              # acorde de dó maior
G4:1/2~ | G4:1/4            # ligadura: vira um sol só, de 3/4
```

**Dinâmica.** `pp`, `p`, `mp`, `mf` (padrão), `f` e `ff` valem para as notas seguintes da faixa.

**Barras e repetições.** `|` é barra de compasso e é conferida: a duração entre barras tem de fechar o compasso. O primeiro compasso pode ser menor (anacruse). `|: ... :|` toca o trecho duas vezes; um `:|` sem `|:` repete desde o começo da faixa (ou desde o `:|` anterior).

**Bateria.** Na faixa com `instrumento bateria`, as notas são `bumbo`, `caixa`, `chimbal`, `chimbal_aberto` e `prato`. Acordes também valem: `[bumbo chimbal]:1/8`.

**Comentários.** `#` no início de uma palavra começa um comentário até o fim da linha. Dentro de uma palavra (`C#4`) é sustenido.

Um exemplo completo:

```
# Uma frase curta com acompanhamento
tempo 96
compasso 4/4

faixa melodia
instrumento sino
pan 20
mf
|: E5:1/8 G5 C6:1/4 B5:1/8 G5 E5:1/4 | D5:1/4. E5:1/8 C5:1/2 :|

faixa base
instrumento piano
volume 60
p
|: [C3 E3 G3]:1/2 [A2 C3 E3] | [F2 A2 C3] [G2 B2 D3] :|
```

## Como compilar

Com make (o binário fica em `build/`):

```sh
make
make test
make clean
```

Com CMake:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Precisa só de um compilador C11 (clang ou gcc) e da biblioteca padrão: o código não usa nada específico de sistema operacional.

## Testes

```sh
make test
```

- `tests/test_dsp.c`: seno e exponencial próprios contra a libm; frequência das notas (A4 = 440 Hz, f = 440 * 2^((n - 69) / 12)); aliasing medido com FFT (PolyBLEP contra osciladores ingênuos); valores do ADSR nos pontos-chave; resposta dos filtros na frequência de corte; pan de potência constante; limitador contínuo, monótono e limitado; passa-tudo que preserva energia; queda de 60 dB da reverberação.
- `tests/test_partitura.c`: alturas (letras, solfejo, acidentes), durações (pontos, quiálteras), acordes, ligaduras, repetições, dinâmica, anacruse e dezenas de erros com linha e coluna exatas.
- `tests/test_render.c`: tempo em amostras, bytes do cabeçalho WAV, little-endian, altura de cada instrumento medida pela DFT, silêncio antes da primeira nota, pan, renderização igual com blocos de qualquer tamanho, nenhuma amostra fora dos 16 bits e o hash de uma renderização curta.
- `tests/wav.sh`: gera todos os exemplos e confere os arquivos com o módulo `wave` do Python e, no macOS, com o `afinfo`.

Com AddressSanitizer e UndefinedBehaviorSanitizer:

```sh
make clean
make test CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
          LDFLAGS="-fsanitize=address,undefined"
```

## Como funciona

### Amostragem: som é uma lista de números

O som é a pressão do ar variando no tempo. Para guardar isso num computador, medimos a pressão em intervalos regulares: são as **amostras**. A 44 100 amostras por segundo (a taxa do CD), um segundo de áudio estéreo são 88 200 números de 16 bits.

O teorema de Nyquist diz que, com taxa `fs`, só dá para representar frequências até `fs / 2` (22 050 Hz aqui, acima do que o ouvido humano escuta). Uma frequência acima disso não some: ela "dobra" e aparece como outra, mais grave e sem relação harmônica com a nota. Isso é o **aliasing**, e é o maior inimigo de quem escreve um sintetizador.

### Da partitura às notas

O analisador (`src/partitura.c`) lê o texto caractere a caractere, guardando linha e coluna. O tempo musical é contado em **ticks**: 768 por semibreve. O número foi escolhido para que toda duração aceita seja um inteiro exato: 768 é divisível por 64 (a menor figura), e os fatores 3/2 (ponto), 7/4 (dois pontos) e 2/3 (quiáltera) continuam dando inteiros. Assim, uma música de dez minutos termina exatamente onde deveria, sem acumular erro de arredondamento.

Só no fim os ticks viram amostras, com aritmética inteira: `amostras = ticks * 60 * 44100 / (tempo * 192)`, arredondado.

A frequência de cada nota vem do temperamento igual: cada semitom multiplica a frequência por 2^(1/12), e o lá central (nota MIDI 69) é 440 Hz:

```
f = 440 * 2^((n - 69) / 12)        C4 (n = 60) = 261,63 Hz
```

### Osciladores

Um oscilador guarda uma **fase** de 0 a 1 e, a cada amostra, soma `dt = f / 44100`. A forma de onda é uma função da fase:

```
dente de serra: 2 * fase - 1
quadrada:       +1 se fase < largura, senão -1
seno:           sen(2 * pi * fase)
```

O dente de serra e a quadrada têm saltos instantâneos, e um salto tem harmônicos infinitos. Os que passam de 22 050 Hz dobram e viram aliasing.

### PolyBLEP: o salto com banda limitada

Um salto "de verdade", com banda limitada, não é um degrau: ele tem uma pequena rampa em volta. O **PolyBLEP** (polynomial band-limited step, proposto por Välimäki e Huovilainen) corrige só as duas amostras vizinhas de cada salto, somando a diferença entre essa rampa (aproximada por um polinômio de grau 2) e o degrau ingênuo:

```c
double poly_blep(double t, double dt)
{
    if (t < dt) {               /* logo depois do salto */
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {         /* logo antes do salto */
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;                 /* longe do salto: nada muda */
}

double serra = 2.0 * fase - 1.0 - poly_blep(fase, dt);
```

Custa quase nada e funciona bem. O teste `test_dsp` mede o efeito com uma FFT, numa nota aguda (1234 Hz, com muitos harmônicos acima de Nyquist), somando a energia fora dos harmônicos:

| Oscilador | Aliasing até 5 kHz | Aliasing na banda toda |
|---|---|---|
| dente de serra ingênuo | -21,3 dB | -14,4 dB |
| dente de serra com PolyBLEP | -67,3 dB | -29,9 dB |
| pulso de 25% ingênuo | -22,0 dB | -14,8 dB |
| pulso de 25% com PolyBLEP | -71,8 dB | -29,6 dB |

Na faixa mais audível, o alias cai mais de 45 dB. O que sobra fica perto de 22 kHz, onde quase ninguém escuta.

O triângulo é feito integrando a onda quadrada com PolyBLEP (a integral de uma quadrada é um triângulo), e o ruído branco vem de um gerador xorshift de 32 bits.

### Envelope ADSR

Um instrumento não liga e desliga de uma vez: o volume segue um **envelope** de quatro partes:

```
nível
  1 |   /\
    |  /  \______________
  S | /                  \
    |/                    \
  0 +----+---+-----------+----+--> tempo
     A    D   (segura)    R
          nota apertada   nota solta
```

**A**taque (sobe de 0 a 1), **D**ecaimento (cai até o nível de **S**ustentação) e **R**elease (cai a zero depois que a nota é solta). Se a nota for solta no meio do ataque, o release parte do nível em que ela estava, para não haver um degrau (que soaria como um estalo). Cada instrumento tem o seu: a flauta tem ataque lento (70 ms), o chiptune quase instantâneo (2 ms).

### Filtros

- **Um polo** (`y += a * (x - y)`): o passa-baixas mais simples que existe, uma média móvel exponencial.
- **Biquad**: filtro de segunda ordem com as fórmulas do "Audio EQ Cookbook" de Robert Bristow-Johnson, em passa-baixas, passa-altas e passa-banda. O baixo usa um passa-baixas ressonante cuja frequência de corte fecha logo depois do ataque, o clássico "uau" dos sintetizadores analógicos.

### FM: sinos e piano elétrico

Na síntese por modulação de frequência (John Chowning, Stanford, 1973; famosa no Yamaha DX7), uma senoide modula a fase de outra:

```
y = sen(2 * pi * fc * t + I * sen(2 * pi * fm * t))
```

O índice `I` controla quantos harmônicos aparecem. Com `fm = fc`, os parciais são harmônicos e o resultado lembra um piano elétrico; o índice cai com o tempo, então o som começa brilhante e fica mais puro. Com `fm = 1,4 * fc`, os parciais não são múltiplos da fundamental, e é exatamente isso que soa como metal: um sino.

### Bateria sintetizada

- **Bumbo**: uma senoide cuja frequência despenca de 168 Hz para 48 Hz em poucos milissegundos, mais um estalo de ruído no ataque.
- **Caixa**: um tom curto (a pele) somado a ruído por um passa-altas (a esteira).
- **Chimbal e prato**: seis ondas quadradas com frequências inarmônicas, como na bateria eletrônica TR-808, mais ruído, por um passa-altas, com decaimento curto (chimbal fechado) ou longo (aberto e prato).

### Mixagem: pan e limitador

O pan usa a lei de **potência constante**: o ângulo vai de 0 a 90 graus e `esquerda = cos`, `direita = sen`. Como cos² + sen² = 1, o som não fica mais fraco no meio do estéreo (no centro, cada lado recebe 0,707, ou -3 dB).

Somar várias faixas pode passar de 1,0 (o máximo de um WAV). Em vez de cortar seco (o que distorce feio), o **limitador suave** é linear até 0,75 e depois usa uma curva `tanh` que se aproxima de 1 sem nunca passar. A curva é contínua e tem a mesma inclinação no ponto de junção, então não cria quinas.

### Reverberação de Schroeder

Em 1962, Manfred Schroeder publicou a primeira reverberação digital que soava natural:

- **4 filtros pente em paralelo**: cada um é um atraso com realimentação (`saída = entrada + g * saída de D amostras atrás`), que gera uma série de ecos que vão sumindo. Os atrasos (29,7 ms, 37,1 ms, 41,1 ms e 43,7 ms) não têm divisores em comum, para os ecos não coincidirem. O ganho `g` sai do tempo de reverberação desejado: para cair 60 dB em `rt60` segundos, `g = 10^(-3 D / (rt60 * fs))`.
- **2 filtros passa-tudo em série** (5 ms e 1,7 ms): têm ganho 1 em todas as frequências, mas embaralham a fase e deixam os ecos mais densos, sem cara de "eco de cânion".

Como no Freeverb, há um passa-baixas dentro da realimentação dos pentes (os agudos morrem antes, como numa sala de verdade) e o canal direito usa atrasos 23 amostras mais longos, o que dá a sensação de estéreo.

### O formato WAV

Um WAV é um arquivo RIFF: blocos com um nome de 4 letras, um tamanho de 32 bits e o conteúdo. O cabeçalho deste sintetizador tem 44 bytes:

| Posição | Bytes | Conteúdo |
|---|---|---|
| 0 | 4 | `RIFF` |
| 4 | 4 | tamanho do resto do arquivo (36 + dados) |
| 8 | 4 | `WAVE` |
| 12 | 4 | `fmt ` (com o espaço) |
| 16 | 4 | 16, o tamanho do bloco fmt |
| 20 | 2 | 1 = PCM sem compressão |
| 22 | 2 | 2 canais |
| 24 | 4 | 44100 amostras por segundo |
| 28 | 4 | 176400 bytes por segundo |
| 32 | 2 | 4 bytes por quadro (2 canais x 2 bytes) |
| 34 | 2 | 16 bits por amostra |
| 36 | 4 | `data` |
| 40 | 4 | tamanho dos dados em bytes |

Depois vêm as amostras, intercaladas (esquerda, direita, esquerda, ...), como inteiros de 16 bits com sinal. Todos os números são **little-endian** (o byte menos significativo primeiro). O código escreve cada byte com deslocamentos (`v & 0xFF`, `v >> 8`) em vez de copiar a memória, então o arquivo sai certo até numa máquina big-endian.

### Mesmo arquivo em qualquer máquina

Seno, cosseno e exponencial da libm podem diferir no último bit entre sistemas, e o compilador pode trocar `a * b + c` por uma instrução FMA, que arredonda de outro jeito. Uma diferença no último bit às vezes muda o arredondamento para 16 bits e, com ela, o arquivo inteiro.

Por isso `src/dsp.c` calcula seno e exponencial com polinômios próprios (séries de Taylor depois de reduzir o argumento, com erro da ordem de 1e-15), usando só operações que o IEEE 754 garante iguais em qualquer máquina, e o build usa `-ffp-contract=off`. O teste confere o hash de uma renderização curta, e ele é o mesmo com `-O0`, `-O3` e com os sanitizers.

## Contribuindo

Issues e pull requests são bem-vindos.

## Licença

MIT. Veja [LICENSE](LICENSE).
