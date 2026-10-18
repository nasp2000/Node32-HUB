Import("env")

import os
import glob
import shutil
import re

def remove_files_recursive(root_dir, patterns):
    removed = 0
    for pattern in patterns:
        for f in glob.glob(os.path.join(root_dir, "**", pattern), recursive=True):
            if os.path.isfile(f):
                os.remove(f)
                removed += 1
    return removed

def remove_tree(dir_path):
    if os.path.isdir(dir_path):
        shutil.rmtree(dir_path, ignore_errors=True)
        print(f"remove_arch_asm: removed directory tree {dir_path}")

project = env.subst("$PROJECT_DIR")
env_name = env.subst("$PIOENV")

managed = os.path.join(project, "managed_components")

# Remove test/fuzzing/example directories from managed components.
# PlatformIO scans all source files in managed_components/ and compiles them
# regardless of the component's CMakeLists.txt, causing missing header errors.
for root, dirs, _ in os.walk(managed):
    for skip_dir in ("tests", "test", "test_sim", "test_apps", "testdir", "testwrap",
                      "fuzzing", "examples", "example", "external_examples", "applications",
                      "runners", "benches"):
        if skip_dir in dirs:
            remove_tree(os.path.join(root, skip_dir))

if env_name == "esp32p4":
    # Remove ARM assembly from the MP3 decoder.
    asm_dir = os.path.join(managed, "chmorgan__esp-libhelix-mp3", "libhelix-mp3", "real", "arm")
    if os.path.isdir(asm_dir):
        n = remove_files_recursive(asm_dir, ["*.s", "*.S"])
        if n:
            print(f"remove_arch_asm: removed {n} ARM assembly file(s) from {asm_dir}")

    # Remove Xtensa (ae32/aes3) assembly files — not valid on RISC-V P4.
    # These are found in esp-dsp and dl_fft managed components.
    xtensa_patterns = ["*ae32.S", "*ae32_.S", "*aes3.S", "*_ae32.s"]
    n = remove_files_recursive(managed, xtensa_patterns)
    if n:
        print(f"remove_arch_asm: removed {n} Xtensa assembly file(s) from managed_components")

    # Remove Xtensa ISA directory trees (dl_fft).
    for root, dirs, _ in os.walk(managed):
        for isa_dir in ("esp32", "esp32s3"):
            if isa_dir in dirs:
                remove_tree(os.path.join(root, isa_dir))

    # Remove x86/x86_64 assembly from libsodium (sandy2x, xmm6).
    for root, dirs, _ in os.walk(managed):
        for x86_dir in ("sandy2x", "xmm6"):
            if x86_dir in dirs:
                remove_tree(os.path.join(root, x86_dir))

    # Remove broken/unused managed components that don't compile on P4 RISC-V.
    # These are transitive deps not used by our firmware.
    broken_components = [
        "espressif__dl_fft",     # calls Xtensa-only FFT functions, no _ansi fallback
        "espressif__eppp_link",  # missing Kconfig symbols
        "espressif__esp_hosted", # missing include paths, not used
        "espressif__esp-sr",     # not used, adds ~3min compile time
        "espressif__esp_modem",              # port/linux/ code doesn't compile on ESP32, not used
        "espressif__esp_serial_slave_link",  # missing include paths, not used
        "espressif__esp_wifi_remote",        # depends on eppp_link (removed), not used
        "espressif__wifi_remote_over_eppp",  # depends on eppp_link (removed), not used
        "espressif__libsodium",              # missing include paths for internal headers
    ]
    for comp in broken_components:
        remove_tree(os.path.join(managed, comp))

    # Remove rogue test.c files with their own main() (e.g. cJSON).
    n = remove_files_recursive(managed, ["test.c", "test.cpp"])
    if n:
        print(f"remove_arch_asm: removed {n} test file(s) from managed_components")
