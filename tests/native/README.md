# Native C++ Unit Tests

Testes compilados **fora do ESP32** (em PC) com [doctest](https://github.com/doctest/doctest).
Testam funções puras sem dependências de hardware — compilam com `g++` em Windows (LLVM MinGW)
ou Linux.

## Estrutura

```
tests/native/
├── Makefile              # build + clean, suporta COVERAGE=1
├── coverage.py           # relatório de coverage (gcov/llvm-cov)
├── doctest.h             # framework header-only (v2.5.0)
├── test_*.cpp            # suites de teste
├── stubs/
│   ├── Arduino.h         # stub mínimo (String, macros PROGMEM)
│   └── commands.h        # stub de sendVirtualMessage
└── README.md
```

## Suites

| Suite | Ficheiro | Estratégia | Produção testada |
|---|---|---|---|
| **CRC16-Modbus** | `test_modbus_crc.cpp` | `#include modbus_crc.h` (header extraído) | `modbus_crc.h` |
| **Charset conversion** | `test_charset.cpp` | compila `charset_conversion.cpp` + stub | `charset_conversion.cpp` |
| **String utils** | `test_string_utils.cpp` | compila `string_utils.cpp` (cópia build-time) | `string_utils.cpp` |
| **Recipient matching** | `test_recipient_matching.cpp` | header extraído | `recipient_matching_utils.h` |
| **Unikon checksum** | `test_unikon_checksum.cpp` | header extraído | `unikon_checksum.h` |
| **GRBL parser** | `test_grbl_parser.cpp` | header extraído | `grbl_line_parser.h` |

**Total: 424 test cases, 1432 assertions (CHECK), 43 REQUIRE.**

> Nota: 12 suites compilam produção real (header extraído / .cpp com stub /
> cópia build-time), 3 usam mocks (alert_pipeline, sensor_config_cycle,
> handler_post_persist), e as restantes são standalone reimpls. As reimpls
> podem derivar da produção — preferir estratégia 2/3 (AGENTS.md) ao adicionar.

### Três estratégias (ver AGENTS.md)
1. **Standalone reimpl** — código com dependências profundas (ArduinoJson, etc.).
2. **Compilar prod source** — função com poucas dependências, stubs cobrem.
3. **Extrair header** — função usada em vários locais, header standalone.

## Como correr

### Opção 1: Pre-script do PlatformIO (automático)
```bash
pio run -e n16r8               # corre testes antes de compilar firmware
SKIP_NATIVE_TESTS=1 pio run    # salta testes
```
Falha o build se algum teste falhar.

### Opção 2: Script runner (recomendado)
```bash
python ../../chatgpt/scripts/run_native_tests.py
```
Faz build + run de todas as suites. Flags:
- `SKIP_NATIVE_TESTS=1` — salta
- `FORCE_NATIVE_TESTS=1` — falha se toolchain não encontrado

### Opção 3: Makefile direto
```bash
cd tests/native
mingw32-make clean all         # Windows
make clean all                 # Linux
./test_modbus_crc.exe          # correr uma suite específica
```

### Opção 4: build_firmware.bat
No projeto raiz, opção **8** — "Run native C++ tests only (no firmware build)".

## Coverage

```bash
python tests/native/coverage.py               # sumário texto
python tests/native/coverage.py --html        # HTML (Linux, precisa gcovr)
python tests/native/coverage.py --keep        # conserva .gcda/.gcno
```

**Windows/LLVM MinGW**: coverage com C++ exceptions pode crashar — o script
deteta e salta gracefulmente.  
**Linux/CI**: `gcov` + `lcov`/`gcovr` funcionam nativamente.

Coverage atual: apenas `charset_conversion.cpp` compila production source
(69/172 linhas, 40.1%). As restantes suites usam standalone reimpl.

## Como adicionar uma suite nova

1. Criar `tests/native/test_<name>.cpp` com:
   ```cpp
   #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
   #include "doctest.h"
   TEST_CASE("descrição") { CHECK(...); }
   ```
2. Se precisar de stubs: `#include "Arduino.h"` ou `#include "commands.h"`.
3. Registar em dois locais:
   - `tests/native/Makefile` — adicionar stem a `TEST_SRCS` + regra de build
   - `chatgpt/scripts/run_native_tests.py` — adicionar stem a `TEST_BINS`
4. Correr: `python ../../chatgpt/scripts/run_native_tests.py`

## Stubs

- **`Arduino.h`**: `String` (wraps `std::string`), `byte`, `boolean`, macros PROGMEM.
- **`commands.h`**: `sendVirtualMessage()` no-op.

## Toolchain

O projeto usa **LLVM MinGW** (instalado via winget). O script de CI deteta
automaticamente em `LOCALAPPDATA/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW.*/llvm-mingw-*/bin/`.

Se `g++` estiver no PATH, usa esse directamente.

## CI

O ficheiro `.github/workflows/test.yml` tem um job `native` comentado que
corre em `ubuntu-latest`. Descomentar para ativar.
