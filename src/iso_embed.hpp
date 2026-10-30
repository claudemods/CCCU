#pragma once

namespace cccu::iso_embed {

struct Blob {
    const char* begin;
    const char* end;
    unsigned long long size() const { return static_cast<unsigned long long>(end - begin); }
};

// Compiled into the cccu binary (see iso_embed.cpp), so `cccu iso` needs no files next to it.
Blob engine();        // cccu-iso-engine: the ISO engine, a static program that runs inside the container
Blob provision();     // build.sh of claudebackup-linux: its --root-phase installs the ISO tools and Calamares
Blob calamares();     // calamares-files.zip
Blob claudemods();    // claudemods.zip (Calamares branding)
Blob bootloaders();   // BIOS/UEFI boot files for the non-Arch families (empty when the build had no network: then the
                      // provisioning downloads them in the container)

}  // namespace cccu::iso_embed
