// cccu-iso-engine: the claudebackup-linux ISO engine (claudebackup-linux-main/dev-release/src), compiled in as
// CCCU's own code. `cccu iso` carries this program inside itself and runs it INSIDE the Incus container, where the
// container's own kernel, package tree and tools are. It is linked statically so it starts in any container,
// whatever C library that container has.
//
//   cccu-iso-engine [--name f.iso] [--out dir] [--work dir] [--tree-dir dir] [--clone-dir dir] [--tag label]
//                   [--level 1-22] [--format squashfs|erofs] [--ram|--no-ram] [--clean-cache|--keep-cache] [--yes]
//
// The last line printed on success is "CBL_ISO=<path>". CBL_KERNEL=<release> names the kernel to boot.

#include "remaster.h"

int main(int argc, char* argv[]) {
    if (geteuid() != 0) { logError("The ISO engine needs root."); return 1; }

    RemasterOptions o;
    o.level = 3;
    o.ramMode = 1;
    o.cleanCache = true;
    o.imageFormat = "squashfs";
    std::string treeDir;

    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto val = [&](std::string& out) {
            if (i + 1 >= argc) { logError("Missing value for " + a); return false; }
            out = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--name") { if (!val(o.isoName)) return 2; }
        else if (a == "--out") { if (!val(o.outputDir)) return 2; }
        else if (a == "--work") { if (!val(o.workDir)) return 2; }
        else if (a == "--clone-dir") { if (!val(o.cloneDir)) return 2; }
        else if (a == "--tree-dir") { if (!val(treeDir)) return 2; }
        else if (a == "--tag") { if (!val(o.isoTag)) return 2; }
        else if (a == "--level") { if (!val(v)) return 2; o.level = atoi(v.c_str()); }
        else if (a == "--format") { if (!val(v)) return 2; o.imageFormat = (v == "erofs") ? "erofs" : "squashfs"; }
        else if (a == "--ram") o.ramMode = 1;
        else if (a == "--no-ram") o.ramMode = 0;
        else if (a == "--clean-cache") o.cleanCache = true;
        else if (a == "--keep-cache") o.cleanCache = false;
        else if (a == "--yes" || a == "-y") {}
        else { logError("Unknown option: " + a); return 2; }
    }

    const Distro distro = detectDistro();
    if (!distro.supported()) { logError("Unsupported system: " + distro.prettyName); return 1; }
    if (o.level < 1 || o.level > 22) o.level = 3;
    if (!distro.erofsSupported()) o.imageFormat = "squashfs";
    o.algo = remaster::compressionFor(distro);
    if (o.isoName.empty()) o.isoName = makeIsoName(distro);
    else if (!endsWith(o.isoName, ".iso")) o.isoName += ".iso";
    if (!treeDir.empty()) o.isoTreeDir = treeDir;

    // Arch: mkinitcpio needs claudemods' 11-dm-initramfs.rules (taken from the embedded boot tree)
    if (distro.family == "archlinux" && !remaster::mkinitcpioConfigReady() && !remaster::ensureMkinitcpioConfig()) return 1;

    const std::string outDir = !o.outputDir.empty() ? o.outputDir : (!o.workDir.empty() ? o.workDir : DEFAULT_WORK_DIR);
    mkdirP(outDir);
    if (!remaster::run(o)) return 1;
    std::cout << "CBL_ISO=" << outDir << "/" << o.isoName << std::endl;
    return 0;
}
