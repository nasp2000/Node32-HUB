# Git Hooks — Node32-HUB

## Activation

Run once to point Git at this hooks directory:

```bash
git config core.hooksPath .githooks
```

This is a repo-local setting (not global), so it's per-checkout.

## What the pre-commit hook does

1. **scan_secrets.py** — checks staged files for hardcoded passwords, tokens, API keys
2. **lint_comments.py** — checks for Portuguese comments in source code
3. **validate_i18n.py** — verifies UI translation keys are in sync (EN ↔ PT)
4. **validate_packs.py** — validates pack configurations
5. **validate_feature_flags.py** — checks feature flag consistency
6. **format_check.py** — checks C++ formatting with clang-format (skipped if not installed)

If any check fails, the commit is blocked.

## Skipping

To bypass hooks temporarily:

```bash
git commit --no-verify
```
