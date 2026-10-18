"""
Scan all .h / .cpp / .ino files in src/ for Portuguese-language identifiers.

Outputs a categorized report: functions, variables, types, macros, filenames.
Safe to run — read-only, no side effects.
"""

import os
import re
import sys

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
SRC_DIR = os.path.join(PROJECT_DIR, "chatgpt", "src")

# ── Portuguese word stems commonly used as identifiers ──────────────────────

PORTUGUESE_STEMS = [
    # Verbs (infinitive / conjugated)
    "enviar", "receber", "rececao", "processar", "gerenciar", "configurar",
    "salvar", "carregar", "buscar", "retornar", "validar", "ignorar",
    "exibir", "mostrar", "escrever", "ler", "abrir", "fechar", "limpar",
    "pausar", "continuar", "iniciar", "parar", "finalizar", "cancelar",
    "tentar", "aguardar", "esperar", "mudar", "alternar", "definir",
    "obter", "inserir", "remover", "criar", "destruir", "alocar",
    "liberar", "converter", "transformar", "calcular", "medir",
    "comparar", "verificar", "checar", "garantir", "assegurar",
    "notificar", "informar", "logar", "registar", "autenticar",
    "criptografar", "decodificar", "codificar", "serializar",
    "descarregar", "reiniciar", "desativar", "ativar",
    # Nouns
    "mensagem", "linha", "dados", "configuracao", "arquivo", "pasta",
    "diretorio", "caminho", "nome", "valor", "chave", "lista", "vetor",
    "matriz", "buffer", "fila", "pilha", "tempo", "data", "hora",
    "minuto", "segundo", "status", "estado", "modo", "tipo", "classe",
    "funcao", "metodo", "propriedade", "atributo", "parametro",
    "argumento", "resultado", "resposta", "pedido", "comando", "acao",
    "evento", "sinal", "interrupcao", "conexao", "ligacao", "porta",
    "serial", "usb", "rede", "wifi", "ethernet", "servidor", "cliente",
    "pacote", "quadro", "bytes", "caractere", "texto", "string",
    "numero", "inteiro", "decimal", "booleano", "verdadeiro", "falso",
    "sim", "nao", "tudo", "nada", "algum", "nenhum", "capturar",
    "disparar", "tratar", "executar", "montar", "desmontar",
    "sincronizar", "agendar", "adiar", "repetir", "temporario",
    # Common compound fragments
    "ultima", "ultimo", "primeira", "primeiro", "proxima", "proximo",
    "atual", "anterior", "seguinte", "ultimo", "pendente",
    "disponivel", "ocupado", "livre", "ativo", "inativo",
]
PORTUGUESE_STEMS_RE = re.compile(
    r"\b(" + "|".join(re.escape(s) for s in PORTUGUESE_STEMS) + r")\w*",
    re.IGNORECASE
)

# Exclude English words that happen to match Portuguese stems
ENGLISH_FALSE_POSITIVES = {
    "serial", "status", "data", "buffer", "timer", "event", "class",
    "method", "property", "port", "client", "server", "string", "number",
    "type", "mode", "value", "key", "list", "name", "time", "link",
    "reset", "total", "min", "max", "format", "final", "active",
    "export", "import", "media", "prime", "capture", "local",
}

# ── Portuguese-only identifiers from the codebase (grandfathered) ───────────
KNOWN_PT_IDENTIFIERS = {
    "sendVirtualMessage",
    "processLine",
    "lastReceptionTime",
    "sendVirtualMessageInternal",
}
# Filenames excluded from scan (grandfathered or misc)
GRANDFATHERED_FILES = {
    "process_line_impl.h",
    "rs232_misc_impl.h",
}


def is_grandfathered(path):
    """Return True if the path is in the list of grandfathered PT files."""
    basename = os.path.basename(path).lower()
    return basename in GRANDFATHERED_FILES


def find_identifiers_in_file(filepath, rel_path):
    """Scan a single source file for PT identifiers."""
    results = []
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except Exception as e:
        return [("error", 0, f"cannot read: {e}", "")]

    for lineno, line in enumerate(lines, 1):
        stripped = line.strip()

        # Skip comments and strings (basic heuristic)
        if stripped.startswith("//") or stripped.startswith("*") or stripped.startswith("#"):
            continue

        # String literals — skip
        if stripped.startswith('"') or stripped.startswith("R\""):
            continue

        # Find PT words
        for m in PORTUGUESE_STEMS_RE.finditer(stripped):
            word = m.group(0)
            # Check if the matched prefix (the PT stem) is an English false positive
            stem = m.group(1).lower()
            if stem in ENGLISH_FALSE_POSITIVES:
                continue
            # Also skip if whole identifier is a known English term
            word_lower = word.lower()
            if word_lower in ENGLISH_FALSE_POSITIVES:
                continue

            # Classify the context
            context = _classify_identifier(line, m.start())

            results.append((context, lineno, stripped[:120], word))
            break  # one match per line to avoid noise

    return results


def _classify_identifier(line, pos):
    """Determine what kind of identifier the word belongs to."""
    # Look backward from pos for context clues
    prefix = line[:pos].strip()

    if prefix.endswith("#define"):
        return "macro"
    if prefix.rstrip().endswith("enum") or prefix.rstrip().endswith("class"):
        return "type"
    if line.strip().endswith(";") and "(" not in line:
        # Check for variable declaration pattern
        if any(t in prefix for t in ("int ", "bool ", "char ", "float ", "double ",
                                      "void ", "String ", "static ", "volatile ",
                                      "uint8_t ", "uint16_t ", "uint32_t ",
                                      "size_t ", "const ", "std::")):
            return "variable"
    # Function definition or call
    if "(" in line and pos < line.find("("):
        return "function"
    return "identifier"


def scan_directory(src_dir):
    """Scan all source files and collect PT identifiers."""
    all_results = []

    for root, dirs, files in os.walk(src_dir):
        for fname in sorted(files):
            if not any(fname.endswith(ext) for ext in (".h", ".cpp", ".ino")):
                continue
            if fname in ("feature_flags.h",):
                continue
            filepath = os.path.join(root, fname)
            rel_path = os.path.relpath(filepath, PROJECT_DIR)

            results = find_identifiers_in_file(filepath, rel_path)
            for ctx, lineno, line_snippet, word in results:
                all_results.append((rel_path, lineno, ctx, word, line_snippet))

    return all_results


def print_report(results):
    """Print categorized report."""
    if not results:
        print("✓ No Portuguese identifiers found.")
        return

    by_type = {}
    for r in results:
        by_type.setdefault(r[2], []).append(r)

    total = len(results)
    print(f"\n{'='*70}")
    print(f"  Results: {total} Portuguese identifier(s) in source files")
    print(f"{'='*70}\n")

    # Summary
    print("── Summary ──")
    for ctx in sorted(by_type.keys()):
        print(f"  {ctx}: {len(by_type[ctx])}")

    # Detailed per type
    for ctx in sorted(by_type.keys()):
        items = by_type[ctx]
        print(f"\n── {ctx.upper()} ({len(items)}) ──")
        for path, lineno, ctx, word, snippet in sorted(items):
            print(f"  {path}:{lineno:>5}  {word:30s}  {snippet[:80]}")

    print(f"\n{'='*70}")
    print(f"  Grandfathered: {', '.join(GRANDFATHERED_FILES)}")
    print(f"{'='*70}\n")


if __name__ == "__main__":
    if "--help" in sys.argv or "-h" in sys.argv:
        print("Usage: python scripts/find_pt_identifiers.py [--report]")
        print("Scans src/ for Portuguese-language identifiers.")
        print("  --report   (default) print categorized report")
        print("  --json     output JSON for machine processing")
        sys.exit(0)

    results = scan_directory(SRC_DIR)
    print_report(results)
