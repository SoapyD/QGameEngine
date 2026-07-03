// Single translation unit that compiles miniaudio + stb_vorbis.
// Everything else includes "miniaudio.h" for declarations only.
//
// stb_vorbis gives miniaudio OGG/Vorbis decoding (our music is .ogg). The
// header-only include must come BEFORE miniaudio's implementation so miniaudio
// detects Vorbis support; the stb_vorbis implementation must come AFTER.

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
