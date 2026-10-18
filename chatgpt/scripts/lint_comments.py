"""
Build-time linter: fail if Portuguese comments are found in src/ and chatgpt/.

Called as a PlatformIO pre-script. Exits with code 1 if any Portuguese comment
is detected (non-grandfathered files only).
"""

import os
import re
import sys

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
SRC_DIRS = [
    os.path.join(PROJECT_DIR, "chatgpt", "src"),
]

PORTUGUESE_WORDS = [
    "aguarda", "aguardar", "alternar", "anterior", "apagar", "arquivo",
    "atual", "atualizar", "buscar", "caminho", "campo", "capturar",
    "caractere", "carregar", "chave", "checar", "classe", "codificar",
    "comando", "configuracao", "configurar", "conexao", "continuar",
    "converter", "criar", "criptografar", "dados", "deve", "deveria",
    "definir", "desativar", "descarregar", "destruir", "desligado",
    "diretorio", "disparar", "disponivel", "efetuar", "enviar",
    "erro", "escrever", "esperar", "estado", "evento", "exibir",
    "executar", "falso", "fechar", "ficheiro", "fila", "finalizar",
    "formatar", "funcao", "gerenciar", "ignorar", "inativo",
    "informar", "iniciar", "inserir", "interrupcao", "inteiro",
    "ligacao", "ligado", "limpar", "linha", "lista", "livre",
    "logar", "matriz", "medir", "mensagem", "metodo", "modo",
    "montar", "mostrar", "mudar", "nulo", "numero", "obter",
    "ocupado", "pacote", "pasta", "pausar", "pedido", "pendente",
    "porta", "primeira", "primeiro", "processar", "propriedade",
    "proxima", "proximo", "quadro", "rede", "registar", "reiniciar",
    "repetir", "resposta", "resultado", "retornar", "salvar",
    "segundo", "serializar", "servidor", "sinal", "sincronizar",
    "string", "temporario", "tentar", "texto", "tipo", "tratar",
    "ultima", "ultimo", "usb", "validar", "valor", "verdadeiro",
    "verificar", "vetor", "wifi",
]

PORTUGUESE_RE = re.compile(
    r"\b(?:" + "|".join(re.escape(w) for w in PORTUGUESE_WORDS) + r")\b",
    re.IGNORECASE,
)

ENGLISH_FALSE_POSITIVES = {
    "active", "buffer", "camp", "class", "client", "data", "event",
    "export", "final", "format", "import", "key", "link", "list",
    "local", "media", "method", "mode", "name", "number", "port",
    "prime", "property", "reset", "second", "serial", "server",
    "status", "string", "time", "timer", "total", "type", "value",
    "usb", "wifi",
}

GRANDFATHERED_FILES = {
    "process_line_impl.h",
    "rs232_misc_impl.h",
}

def find_pt_comments(filepath, rel_path):
    basename = os.path.basename(filepath)
    if basename in GRANDFATHERED_FILES:
        return []

    results = []
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            content = f.read()
    except Exception:
        return []

    lines = content.split("\n")
    in_block_comment = False

    for lineno, line in enumerate(lines, 1):
        stripped = line.strip()

        # Track block comments
        if in_block_comment:
            end = stripped.find("*/")
            if end >= 0:
                in_block_comment = False
                comment_text = stripped[:end]
            else:
                comment_text = stripped
            # Check block comment line for Portuguese
            for m in PORTUGUESE_RE.finditer(comment_text):
                word = m.group(0).lower()
                if word in ENGLISH_FALSE_POSITIVES:
                    continue
                results.append((rel_path, lineno, "block_comment", word, line[:100]))
            continue

        if "//" in stripped:
            comment_start = stripped.index("//")
            comment_text = stripped[comment_start + 2:]
            for m in PORTUGUESE_RE.finditer(comment_text):
                word = m.group(0).lower()
                if word in ENGLISH_FALSE_POSITIVES:
                    continue
                results.append((rel_path, lineno, "line_comment", word, line[:100]))

        if "/*" in stripped and "*/" not in stripped:
            comment_start = stripped.index("/*")
            comment_text = stripped[comment_start + 2:]
            in_block_comment = True
            for m in PORTUGUESE_RE.finditer(comment_text):
                word = m.group(0).lower()
                if word in ENGLISH_FALSE_POSITIVES:
                    continue
                results.append((rel_path, lineno, "block_comment_start", word, line[:100]))
        elif "/*" in stripped:
            comment_start = stripped.index("/*")
            comment_end = stripped.index("*/", comment_start + 2)
            comment_text = stripped[comment_start + 2:comment_end]
            for m in PORTUGUESE_RE.finditer(comment_text):
                word = m.group(0).lower()
                if word in ENGLISH_FALSE_POSITIVES:
                    continue
                results.append((rel_path, lineno, "inline_block", word, line[:100]))

    return results

def main():
    all_issues = []
    for src_dir in SRC_DIRS:
        if not os.path.isdir(src_dir):
            continue
        for root, dirs, files in os.walk(src_dir):
            dirs[:] = [d for d in dirs if d != "languages"]
            for fname in sorted(files):
                if not any(fname.endswith(ext) for ext in (".h", ".cpp", ".ino")):
                    continue
                if fname in ("lang_pt_ui.h", "lang_en_ui.h", "feature_flags.h"):
                    continue
                filepath = os.path.join(root, fname)
                rel_path = os.path.relpath(filepath, PROJECT_DIR)
                issues = find_pt_comments(filepath, rel_path)
                all_issues.extend(issues)

    if all_issues:
        print(f"\n{'='*70}")
        print(f"  LINT FAIL: {len(all_issues)} Portuguese comment(s) found")
        print(f"{'='*70}\n")
        for path, lineno, ctype, word, snippet in sorted(all_issues):
            print(f"  {path}:{lineno:>5}  [{ctype}]  \"{word}\"  {snippet}")
        print(f"\n  Fix by translating these comments to English.")
        print(f"{'='*70}\n")
        sys.exit(1)
    else:
        print("  lint_comments.py: OK — no Portuguese comments found.")
        sys.exit(0)

if __name__ == "__main__":
    main()
