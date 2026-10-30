#include "iso_embed.hpp"

// The files are put into the binary by the assembler (.incbin), like the claudebackup engine does for its Arch
// boot tree. CMake passes their absolute paths.
#define CCCU_EMBED(sym, file)               \
    ".section .rodata\n"                    \
    ".global " sym "_start\n"               \
    ".balign 16\n"                          \
    sym "_start:\n"                         \
    ".incbin \"" file "\"\n"                \
    ".global " sym "_end\n"                 \
    sym "_end:\n"                           \
    ".byte 0\n"                             \
    ".previous\n"

asm(CCCU_EMBED("cccu_embed_engine", CCCU_ENGINE_BIN));
asm(CCCU_EMBED("cccu_embed_provision", CCCU_PROVISION_SH));
asm(CCCU_EMBED("cccu_embed_calamares", CCCU_CALAMARES_ZIP));
asm(CCCU_EMBED("cccu_embed_claudemods", CCCU_CLAUDEMODS_ZIP));

extern "C" {
extern const char cccu_embed_engine_start[], cccu_embed_engine_end[];
extern const char cccu_embed_provision_start[], cccu_embed_provision_end[];
extern const char cccu_embed_calamares_start[], cccu_embed_calamares_end[];
extern const char cccu_embed_claudemods_start[], cccu_embed_claudemods_end[];
}

namespace cccu::iso_embed {

Blob engine() { return {cccu_embed_engine_start, cccu_embed_engine_end}; }
Blob provision() { return {cccu_embed_provision_start, cccu_embed_provision_end}; }
Blob calamares() { return {cccu_embed_calamares_start, cccu_embed_calamares_end}; }
Blob claudemods() { return {cccu_embed_claudemods_start, cccu_embed_claudemods_end}; }

}  // namespace cccu::iso_embed
