"""Pre-build i18n validation.

Checks:
  1. EN/PT UI packs parse and expose the same key set (hard fail on drift).
  2. Every data-i18n* key used by a page asset exists in both packs (hard fail).
  3. Warns on page assets carrying Portuguese fallback text outside a
     data-i18n attribute (AGENTS.md: HTML fallback text must be English).
  4. Warns on pack keys never referenced by any asset (dead keys / typos).

Note on check 3: Portuguese is frequently written with HTML entities
(`&atilde;`, `&ccedil;`, `&#127968;`), so the raw line carries no accented bytes
at all and a plain diacritic scan finds nothing. Every candidate line is
entity-decoded before being tested, otherwise the check silently passes.

Runs inside PlatformIO (SCons `env`) and standalone:
    python3 scripts/validate_i18n.py
"""

import html
import json
import os
import re
import sys

RE_JSON_BLOCK = re.compile(
    r'R"JSON\((.+?)\)JSON"',
    re.DOTALL
)

# data-i18n, data-i18n-html, data-i18n-title, data-i18n-placeholder, ...
# Only these suffixes carry a translation. `data-i18n-ready` is a CSS state flag
# used to hide markup until /api/lang resolves, and must not be read as a key.
RE_DATA_I18N_KEY = re.compile(
    r"""data-i18n(?:-(?:title|html|placeholder|aria|value|alt|label))?\s*=\s*['"]([A-Za-z0-9_]+)['"]"""
)

# Matches a whole data-i18n* attribute, so it can be blanked before scanning.
RE_DATA_I18N_ATTR = re.compile(
    r"""data-i18n(?:-(?:title|html|placeholder|aria|value|alt|label))?\s*=\s*['"][A-Za-z0-9_]+['"]"""
)

# Portuguese-looking characters. Used only to flag fallback text, never to rewrite.
RE_PT_CHARS = re.compile(
    r"[ãõçáéêíóôúàâÃÕÇÁÉÊÍÓÔÚÀÂ]"
)

RE_PT_WORD = re.compile(
    r"\b(?:"
    r"Nunca|Sempre|Configura[çc][ãa]o|Guardad[oa]|Erro|Falha|Aguardar|Utilizador|"
    r"Utilizadores|Ficheiro|Ficheiros|P[áa]gina|P[áa]inas|Defini[çc][õo]es|"
    r"Aguardar|Ligad[oa]|Desligad[oa]|Entrad[oa]|Sa[íi]da|Resposta|"
    r"Apagar|Guardar|Carregar|Actualizar|Atualizar|Enviar|Receber|Ler|Escrever|"
    r"Iniciar|Parar|Continuar|Voltar|Pr[óo]ximo|Anterior|Selecionad[oa]|"
    r"Escolher|Adicionar|Remover|Criar|Eliminar|Apagar|Activo|Ativo|"
    r"Inactivo|Por|Predefined|Padr[ãa]o|Utilizador|Endere[çc]o|"
    r"Rede|Rede|Dispositivo|Dispositivos|Configura[çc][õo]es|Ajustes"
    r")\b"
)

# Identity strings: page / LCD tab names rendered verbatim in the UI (never
# translated, like "Receção de Dados"). Listed here so the fallback check below
# does not report the prose that merely references them.
IDENTITY_STRINGS = re.compile(
    r"(?:\b(?:AGORA|PREVIS[ÃA]O|Rece[çc][ãa]o de Dados|Pombal|P[óo]mbal)\b"
    r"|'Padr[ãa]o'|\"Padr[ãa]o\""
    # language self-names in the language selector: the option must read
    # "Português" in every UI language, like any locale picker.
    r"|<option[^>]*>\s*(?:&#127477;&#127481;\s*)?Portugu[êe]s\s*</option>)"
)

# Protocol sentinels: exchanged verbatim between the browser and the firmware,
# so they must stay byte-identical in both languages ("(não alterado)" is the
# placeholder the form sends for "field left untouched").
PROTOCOL_SENTINELS = re.compile(
    r"\(\s*n(?:ã|a)o\s+alterado\s*\)"
)


def get_env():
    """Return the SCons env when running under PlatformIO, else a stub."""
    try:
        Import("env")  # noqa: F821 - injected by SCons
        return env  # noqa: F821
    except NameError:
        class _Stub:
            def subst(self, s):
                return os.path.abspath(s.replace("$PROJECT_DIR", "."))
        return _Stub()


def extract_json(path):
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    m = RE_JSON_BLOCK.search(content)
    if not m:
        print(f"[validate_i18n] ERROR: could not find R\"JSON(...)JSON\" block in {path}")
        return None
    try:
        data = json.loads(m.group(1))
    except json.JSONDecodeError as e:
        print(f"[validate_i18n] ERROR: invalid JSON in {path}: {e}")
        return None
    return data


def collect_assets(src_dir):
    """Every source file that can carry user-visible markup or JS strings.

    Not just assets/: renderers, route handlers and the shared auto.js also
    emit data-i18n markup and window._t() lookups.
    """
    out = []
    roots = [
        os.path.join(src_dir, "webpages"),
        os.path.join(src_dir, "languages"),
    ]
    for root in roots:
        if not os.path.isdir(root):
            continue
        for dirpath, _dirnames, filenames in os.walk(root):
            for name in sorted(filenames):
                if name.endswith((".cpp", ".h")):
                    out.append(os.path.join(dirpath, name))
    return sorted(out)


def run():
    src_dir = os.path.join(env.subst("$PROJECT_DIR"), "src")
    lang_dir = os.path.join(src_dir, "languages")
    en_file = os.path.join(lang_dir, "lang_en_ui.h")
    pt_file = os.path.join(lang_dir, "lang_pt_ui.h")

    if not os.path.isfile(en_file) or not os.path.isfile(pt_file):
        print("[validate_i18n] SKIP: language files not found")
        return

    en = extract_json(en_file)
    pt = extract_json(pt_file)
    if en is None or pt is None:
        sys.exit(1)

    en_keys = set(en.keys())
    pt_keys = set(pt.keys())
    errors = 0

    # ---- 1. EN <-> PT pack sync -------------------------------------------
    missing_pt = en_keys - pt_keys
    extra_pt = pt_keys - en_keys

    if missing_pt:
        print(f"[validate_i18n] ERROR: {len(missing_pt)} PT key(s) missing (in EN, not in PT):")
        for k in sorted(missing_pt):
            print(f"  MISSING: {k}")
        errors += 1

    if extra_pt:
        print(f"[validate_i18n] WARNING: {len(extra_pt)} PT extra key(s) (not in EN, ignored):")
        for k in sorted(extra_pt):
            print(f"  EXTRA: {k}")

    # ---- 2. data-i18n keys used by assets must exist in both packs --------
    used_keys = {}
    pt_fallbacks = []
    assets = collect_assets(src_dir)
    for path in assets:
        if os.path.basename(path).startswith("lang_"):
            continue
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")
        for lineno, line in enumerate(lines, 1):
            for k in RE_DATA_I18N_KEY.findall(line):
                used_keys.setdefault(k, set()).add(os.path.basename(path))
            # Strip the data-i18n attributes themselves, then look for Portuguese:
            # minified pages put everything on one line, so testing the raw line
            # would hide PT text that sits next to a translated element.
            probe = RE_DATA_I18N_ATTR.sub(" ", line)
            decoded = html.unescape(probe)
            probe = IDENTITY_STRINGS.sub(" ", probe)
            probe = PROTOCOL_SENTINELS.sub(" ", probe)
            decoded = IDENTITY_STRINGS.sub(" ", decoded)
            decoded = PROTOCOL_SENTINELS.sub(" ", decoded)
            # Entity-encode check: `&atilde;`/`&#127968;` hide the diacritics
            # from RE_PT_CHARS, so the decoded copy decides alongside the raw.
            if not (RE_PT_CHARS.search(probe) or RE_PT_WORD.search(probe)
                    or RE_PT_CHARS.search(decoded) or RE_PT_WORD.search(decoded)):
                continue
            # Deliberate bilingual labels ("Modbus Ativo / Active") carry both
            # languages on purpose for mixed-language audiences: not a gap.
            residue = []
            for src in (probe, decoded):
                for m in list(RE_PT_CHARS.finditer(src)) + list(RE_PT_WORD.finditer(src)):
                    tail = src[m.end():m.end() + 3]
                    if not re.match(r"\s*/\s*", tail):
                        residue.append(m.start())
            if not residue:
                continue
            # Report the Portuguese fragment, not the whole minified line.
            i = residue[0]
            start = max(probe.rfind("<", 0, i), probe.rfind(">", 0, i)) + 1
            snippet = probe[start:start + 90].strip()
            pt_fallbacks.append((os.path.basename(path), lineno, snippet))

    # keys defined inside the packs themselves are not "usage"
    missing_used = sorted(k for k in used_keys if k not in en_keys)
    if missing_used:
        print(f"[validate_i18n] ERROR: {len(missing_used)} data-i18n key(s) used by pages but absent from the EN pack:")
        for k in missing_used:
            where = ", ".join(sorted(used_keys[k])[:3])
            print(f"  UNDEFINED: {k}  ({where})")
        errors += 1

    # ---- 3. PT fallback text outside data-i18n (must be English) -----------
    if pt_fallbacks:
        print(f"[validate_i18n] WARNING: {len(pt_fallbacks)} page line(s) with Portuguese text outside data-i18n (fallback must be EN):")
        for name, lineno, snippet in pt_fallbacks[:40]:
            print(f"  PT-FALLBACK: {name}:{lineno}  {snippet}")
        if len(pt_fallbacks) > 40:
            print(f"  ... and {len(pt_fallbacks) - 40} more "
                  f"(raise this cap to see them all; they are sorted by file)")

    # ---- 4. dead keys ------------------------------------------------------
    unused = sorted(en_keys - set(used_keys))
    if unused:
        print(f"[validate_i18n] INFO: {len(unused)} pack key(s) not referenced by any asset (informational).")

    if errors:
        sys.exit(1)

    print(
        f"[validate_i18n] OK: {len(en_keys)} EN keys, {len(pt_keys)} PT keys — "
        f"{len(used_keys)} key(s) used across {len(assets)} asset(s), all synchronized"
    )


env = get_env()
run()