"""
Link the vendored esp_audio_codec prebuilt archive for the n16r8 env.

The pioarduino Arduino flow does not resolve IDF components, so the AAC
decoder (AacDecoder -> esp_aac_dec_*) is provided by a vendored prebuilt
archive (lib/esp_audio_codec/src/lib_esp32s3/libesp_audio_codec.a, Espressif
Modified MIT).

This runs at build-initialization (no pre:/post: prefix) and appends the
archive to LIBS + LIBPATH. SCons places $LIBS at the end of the link command
(after the object files), so the AAC symbols resolve regardless of how
LINKFLAGS is assembled by the Arduino framework.

Only the AAC objects of the archive are pulled in (--gc-sections drops the
rest). No-op for every other env.
"""

Import("env")

import os


def _link_esp_audio_codec(env):
    pioenv = env.subst("$PIOENV")
    if pioenv != "n16r8":
        return

    project_dir = env.subst("$PROJECT_DIR")
    lib_dir = os.path.join(project_dir, "lib", "esp_audio_codec", "src",
                           "lib_esp32s3")
    archive = os.path.join(lib_dir, "libesp_audio_codec.a")
    if not os.path.isfile(archive):
        print("[link_esp_audio_codec] WARNING: archive not found: %s" % archive)
        return

    env.Append(LIBPATH=[lib_dir])
    env.Append(LIBS=["esp_audio_codec"])
    print("[link_esp_audio_codec] linked AAC archive: %s" % archive)


_link_esp_audio_codec(env)
