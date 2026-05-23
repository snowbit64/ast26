# ast26 CLI — Relatório de Auditoria de Flags

**Data:** 2026-05-23  
**Versão:** 1.0.0  
**Total de testes:** 77  
**Aprovados:** 77/77 (100%) — *após correções*  

## Resumo

Antes das correções, 2 de 77 testes falharam (97.4% de sucesso):

| Bug | Flag | Problema | Status |
|-----|------|----------|--------|
| #1 | `-w` / `--resize` | Redimensionamento ignorado — o campo `opts.resize` era parseado mas nunca usado em `encode()` | **Corrigido** |
| #2 | `inspect -t` / `--diff` | Sintaxe `-t file_a file_b` falhava; apenas `-t file_a -t file_b` funcionava | **Corrigido** |

---

## Flags Globais (8/8 — 100%)

| Flag | Descrição | Resultado |
|------|-----------|-----------|
| `-h` / `--help` | Exibir ajuda | ✓ |
| `-v` / `--version` | Versão da biblioteca | ✓ |
| `-l` / `--license` | Texto da licença | ✓ |
| `-i` / `--info` | Metadados de build | ✓ |

## Encoder (25/25 — 100%)

| Flag | Descrição | Resultado |
|------|-----------|-----------|
| `-f` / `--file` | Arquivo de entrada | ✓ |
| `-b` / `--batch` | Entrada em lote | ✓ |
| `-d` / `--dir` | Diretório de entrada | ✓ |
| `-r` / `--recursive` | Varredura recursiva | ✓ |
| `-k 4x4` | Block size 4×4 | ✓ |
| `-k 8x8` | Block size 8×8 | ✓ |
| `-k 12x12` | Block size 12×12 | ✓ |
| `-q fast` | Qualidade rápida | ✓ |
| `-q medium` | Qualidade média | ✓ |
| `-q thorough` | Qualidade alta | ✓ |
| `-s srgb` | Color space sRGB | ✓ |
| `-s linear` | Color space linear | ✓ |
| `-s alpha` | Color space alpha | ✓ |
| `-m 3` | 3 mipmaps extras | ✓ |
| `-m max` | Mipmaps máximo | ✓ |
| `-C rgba32` | Formato de cor RGBA32 | ✓ |
| `-C r8` | Formato de cor R8 | ✓ |
| `-w 128x128` | **Redimensionar** | ✓ *(corrigido)* |
| `-t 2d` | Tipo de textura 2D | ✓ |
| `-n bottomLeft` | Origem bottomLeft | ✓ |
| `-n topLeft` | Origem topLeft | ✓ |
| `-o` / `--output` | Arquivo de saída | ✓ |
| `-u` / `--output-dir` | Diretório de saída | ✓ |
| `-O` / `--overwrite` | Sobrescrever | ✓ |
| `-v` / `--verbose` | Relatório detalhado | ✓ |
| `-x` / `--delete-source-file` | Deletar fonte | ✓ |
| Entrada JPG | Suporte a JPG | ✓ |

## Decoder (19/19 — 100%)

| Flag | Descrição | Resultado |
|------|-----------|-----------|
| `-f` / `--file` | Arquivo de entrada | ✓ |
| `-b` / `--batch` | Entrada em lote | ✓ |
| `-d` / `--dir` | Diretório de entrada | ✓ |
| `-r` / `--recursive` | Varredura recursiva | ✓ |
| `-F png` | Saída PNG | ✓ |
| `-F jpg` | Saída JPG | ✓ |
| `-F astc` | Saída ASTC | ✓ |
| `-F raw-rgba` | Saída raw RGBA | ✓ |
| `-c rgb` | Seleção de canais | ✓ |
| `-i 0` | Mip-index | ✓ |
| `-m` / `--all-mips` | Todos os mipmaps | ✓ |
| `-g` / `--real-origin` | Orientação original | ✓ |
| `-o` / `--output` | Arquivo de saída | ✓ |
| `-u` / `--output-dir` | Diretório de saída | ✓ |
| `-O` / `--overwrite` | Sobrescrever | ✓ |
| `-p` / `--preserve-file-path` | Preservar caminho | ✓ |
| `-v` / `--verbose` | Relatório detalhado | ✓ |
| `-x` / `--delete-source-file` | Deletar fonte | ✓ |
| `-P` / `--pattern` | Padrão de nome | ✓ |

## Inspect (17/17 — 100%)

| Flag | Descrição | Resultado |
|------|-----------|-----------|
| `-f` / `--file` | Arquivo de entrada | ✓ |
| `-b` / `--batch` | Entrada em lote | ✓ |
| `-d` / `--dir` | Diretório de entrada | ✓ |
| `-t` / `--diff` | Comparação de arquivos | ✓ *(corrigido)* |
| `-m` / `--num-mipmaps` | Campo mipmaps | ✓ |
| `-l` / `--num-layers` | Campo layers | ✓ |
| `-c` / `--compression` | Campo compressão | ✓ |
| `-s` / `--size` | Campo tamanho | ✓ |
| `-i` / `--ideal-origin` | Campo origem | ✓ |
| `-S` / `--color-space` | Campo color space | ✓ |
| `-n` / `--channels` | Campo canais | ✓ |
| `-a` / `--all` | Todos os campos | ✓ |
| `-o` / `--output` | Saída JSON | ✓ |
| `-v` / `--verbose` | Relatório detalhado | ✓ |
| Entrada PNG | Inspeção de PNG | ✓ |
| Entrada JPG | Inspeção de JPG | ✓ |

---

## Detalhes das Correções

### Bug #1: `-w` / `--resize` — Redimensionamento não funcionava

**Causa raiz:** O campo `EncodeOptions::resize` era corretamente parseado no CLI (`cli/main.cpp:589-594`), mas a função `encode()` em `src/api.cpp` nunca o utilizava. A imagem de entrada era passada diretamente ao compressor ASTC sem nenhum redimensionamento.

**Correção:** Adicionada função `mip::resize_rgba8()` (bilinear interpolation) em `src/mipgen.cpp` e aplicação de resize na função `encode()` em `src/api.cpp` antes da compressão ASTC.

**Arquivos modificados:**
- `src/mipgen.hpp` — declaração de `resize_rgba8()`
- `src/mipgen.cpp` — implementação de resize bilinear
- `src/api.cpp` — uso de `opts.resize` em `encode()`

### Bug #2: `inspect -t` — Sintaxe `-t file_a file_b` falhava

**Causa raiz:** O parser de argumentos tratava `-t file_a file_b` como `-t` com valor `file_a` e `file_b` como argumento posicional. A função `getall({"t","diff"})` retornava apenas `["file_a"]` e falhava por exigir 2 caminhos.

**Correção:** Modificada a lógica de diff em `run_inspect()` para aceitar o primeiro argumento posicional como segundo caminho quando apenas um valor de `-t` foi encontrado.

**Arquivos modificados:**
- `cli/main.cpp` — lógica de fallback para diff com argumento posicional
