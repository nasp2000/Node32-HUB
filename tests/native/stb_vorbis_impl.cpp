// Separate TU for the stb_vorbis implementation (see test_stb_vorbis_decode.cpp:
// the header leaks macros that collide with doctest.h, so it cannot share a TU
// with the test cases). Built with -I../../chatgpt/lib/stb_vorbis/src.
#include "stb_vorbis.h"
