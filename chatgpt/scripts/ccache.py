Import("env")
import os

try:
    from shutil import which as _which
except Exception:
    _which = None

if _which is None:
    try:
        from distutils.spawn import find_executable as _which
    except Exception:
        _which = None

has_ccache = bool(_which and _which("ccache"))

# Keep build working even if ccache is not installed in PATH.
if has_ccache:
    env.Replace(
        CC="ccache " + env["CC"],
        CXX="ccache " + env["CXX"],
        AS="ccache " + env["AS"],
    )
    os.environ["CCACHE_MAXSIZE"] = "2G"
    print("[ccache] enabled (max 2G)")
else:
    print("[ccache] not found in PATH, build without cache")
