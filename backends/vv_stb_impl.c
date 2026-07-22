// vv_stb_impl.c — the single translation unit that emits the stb_truetype
// implementation for a GUI build.
//
// craz (../craz) deliberately does NOT compile stb_truetype into libcraz.a: its
// font.o only *references* the stbtt_* symbols and leaves the implementation to
// the consuming program (so an app can share one copy). This TU is that copy.
// The vendored header is byte-identical to craz's third_party copy (both v1.26),
// so the stbtt_fontinfo layout matches what craz's font.o was compiled against.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
