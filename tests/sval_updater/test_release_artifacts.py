#!/usr/bin/env python3
"""Host tests for keyboards/svalboard/tools/check_release_artifacts.py (M3).

    test_release_artifacts.py OUT_DIR

Builds a synthetic release directory (for each of the 2 release builds: a
minimal ELF with one loadable segment at 0x10000000, the .uf2, and the .svup
from make_update.py --release), checks that it passes, and that each kind of
substitution is refused.
"""
import contextlib
import importlib.util
import io
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "keyboards/svalboard/tools"
sys.path.insert(0, str(TOOLS))
import check_release_artifacts as cra  # noqa: E402
import make_update as mu  # noqa: E402

spec = importlib.util.spec_from_file_location("tmu", Path(__file__).parent / "test_make_update.py")
tmu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tmu)

VER = "vM3-test"
checks = failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print(f"FAIL: {what}", file=sys.stderr)


def elf(flash, extra_ram=b""):
    """A 32-bit ARM ELF with a PT_LOAD for flash at 0x10000000 and, if given, one for
    .data loaded right after it in flash (paddr) but linked in RAM (vaddr)."""
    segs = [(0x10000000, 0x10000000, flash)]
    if extra_ram:
        segs.append((0x20000000, 0x10000000 + len(flash), extra_ram))
    phoff = 52
    off = phoff + 32 * len(segs)
    hdr = bytearray(b"\x7fELF\x01\x01\x01" + bytes(9))
    hdr += struct.pack("<HHIIIIIHHHHHH", 2, 40, 1, 0x100001F7, phoff, 0, 0x05000200, 52, 32, len(segs), 40, 0, 0)
    ph, body = b"", b""
    for vaddr, paddr, data in segs:
        ph += struct.pack("<8I", 1, off + len(body), vaddr, paddr, len(data), len(data), 5, 4)
        body += data
    return bytes(hdr) + ph + body


def image_for(kb, km, data_tail=b""):
    hand, pointing_id = mu.kb_fields(kb)
    body = bytearray(tmu.image(0x1800))
    rec = struct.pack(mu.BUILD_INFO_FMT, b"SVBI", 3, mu.BI_RELEASE, 0, mu.UPDATER_PROTO, 3,
                      VER.encode().ljust(16, b"\0"), hand, pointing_id, mu.KEYMAP[km], 0)
    body[0x800:0x800 + len(rec)] = rec
    return bytes(body)


def make_svup(src, out, kb, km, raw=False):
    """make_update.py --release, as a release build job runs it."""
    return subprocess.run([sys.executable, "-I", str(TOOLS / "make_update.py"), str(src), *(["--raw"] if raw else []),
                           "-o", str(out), "--kb", kb, "--keymap", km, "--release", "--expect-version", VER],
                          capture_output=True, text=True)


def make_dir(d):
    d.mkdir(parents=True)
    for i, (kb, km) in enumerate(cra.RELEASE_BUILDS):
        n = cra.build_name(kb, km)
        flash = image_for(kb, km)
        data = bytes([i]) * 0x40 if i % 2 else b""  # some builds with a .data segment
        img = flash + data
        img += b"\xff" * (-len(img) % 256)
        (d / f"{n}.elf").write_bytes(elf(flash, data))
        (d / f"{n}_{VER}.uf2").write_bytes(tmu.uf2(img))
        p = make_svup(d / f"{n}_{VER}.uf2", d / f"{n}_{VER}.svup", kb, km)
        check(p.returncode == 0, f"make_update.py --release {n}: {p.stdout}{p.stderr}")


def quiet_main(args):
    with contextlib.redirect_stdout(io.StringIO()):
        return cra.main(args)


def main():
    out = Path(sys.argv[1])
    if out.exists():
        shutil.rmtree(out)
    good = out / "good"
    make_dir(good)
    hashes, problems = cra.check_dir(good, VER)
    check([b for b in cra.RELEASE_BUILDS] == [("svalboard/left", "sval"), ("svalboard/right", "sval")],
          f"the release is the two auto builds (D34): {cra.RELEASE_BUILDS}")
    check(all(mu.kb_fields(kb)[1] == mu.POINTING["auto"] for kb, _ in cra.RELEASE_BUILDS), "release builds are auto")
    check(not problems and len(hashes) == 2, f"a clean directory passes: {problems}")
    check(quiet_main([str(good), "--version", VER]) == 0, "main passes a clean directory")
    check(quiet_main([str(good), "--version", "vOther"]) == 1, "another version refused")

    r, l = "svalboard_right_sval", "svalboard_left_sval"

    def case(what, needle, edit):
        d = out / "case"
        if d.exists():
            shutil.rmtree(d)
        shutil.copytree(good, d)
        edit(d)
        _, probs = cra.check_dir(d, VER)
        check(any(needle in p for p in probs), f"{what}: refused with {needle!r}: {probs}")

    def flip(path, at=0x1000):
        b = bytearray(path.read_bytes())
        b[at] ^= 1
        path.write_bytes(b)

    def resvup(d, n, kb, km):
        """Flip an image byte and make the .svup again from it, so the manifest matches the new image."""
        _, img = mu.parse_svup((d / f"{n}_{VER}.svup").read_bytes())
        img = bytearray(img)
        img[0x1000] ^= 1
        (d / "x.bin").write_bytes(img)
        p = make_svup(d / "x.bin", d / f"{n}_{VER}.svup", kb, km, raw=True)
        check(p.returncode == 0, f"re-made .svup: {p.stdout}{p.stderr}")
        (d / "x.bin").unlink()

    def svup_byte(d, n, at, value):
        b = bytearray((d / f"{n}_{VER}.svup").read_bytes())
        b[at] = value
        (d / f"{n}_{VER}.svup").write_bytes(b)

    case("a flipped image byte with a re-made manifest", "not the ELF's flash contents",
         lambda d: resvup(d, r, "svalboard/right", "sval"))
    case("a flipped .svup image byte", "SHA-512", lambda d: flip(d / f"{r}_{VER}.svup", 108 + 0x1000))
    case("a flipped .uf2 byte", "the .uf2 does not hold the .svup's image", lambda d: flip(d / f"{r}_{VER}.uf2", 32 + 0x10))
    case("another ELF", "not the ELF's flash contents", lambda d: shutil.copy(d / f"{l}.elf", d / f"{r}.elf"))
    case("a left build as right", "the manifest says hand 0", lambda d: [
        shutil.copy(d / f"{l}{x}", d / f"{r}{x}") for x in (".elf",)]
        + [shutil.copy(d / f"{l}_{VER}{x}", d / f"{r}_{VER}{x}") for x in (".uf2", ".svup")])
    def replace_with(d, kb, km):
        """The right release build's three files swapped for another build's (a per-module or blank image)."""
        flash = image_for(kb, km)
        flash += b"\xff" * (-len(flash) % 256)
        (d / f"{r}.elf").write_bytes(elf(flash))
        (d / f"{r}_{VER}.uf2").write_bytes(tmu.uf2(flash))
        p = make_svup(d / f"{r}_{VER}.uf2", d / f"{r}_{VER}.svup", kb, km)
        check(p.returncode == 0, f"make_update.py --release {kb}:{km}: {p.stdout}{p.stderr}")

    case("a blank build as sval", "keymap_id 2", lambda d: replace_with(d, "svalboard/right", "blank"))
    case("a per-module build as the release", "pointing_id 3", lambda d: replace_with(d, "svalboard/trackball/pmw3389/right", "sval"))
    case("a per-module build's files", "unexpected file",
         lambda d: (d / "svalboard_trackpoint_right_sval.elf").write_bytes((d / f"{r}.elf").read_bytes()))
    case("a manifest edited to the other hand", "the manifest says hand 0", lambda d: svup_byte(d, r, 7, 0))
    case("a manifest without RELEASE", "flags 0x0", lambda d: svup_byte(d, r, 10, 0))
    case("a nonzero reserved byte", "reserved", lambda d: svup_byte(d, r, 6, 1))
    case("a version-1 manifest", "version-1", lambda d: svup_byte(d, r, 4, 1))
    case("an extra artifact", "unexpected file", lambda d: shutil.copy(d / f"{r}.elf", d / "extra.elf"))
    case("a missing .svup", "missing", lambda d: (d / f"{r}_{VER}.svup").unlink())
    case("a truncated .svup", "too short", lambda d: (d / f"{r}_{VER}.svup").write_bytes(b"\0" * 10))
    case("a leftover unsigned manifest", "unexpected file", lambda d: (d / f"{r}.manifest").write_bytes(b"\0" * 108))

    # the ELF reader against a non-ELF and an ELF that does not start at XIP_BASE
    bad = out / "bad.elf"
    bad.write_bytes(b"not an elf")
    try:
        cra.elf_flash_image(bad)
        check(False, "a non-ELF is refused")
    except mu.Refused:
        check(True, "a non-ELF is refused")
    raw = bytearray(elf(b"\0" * 0x100))
    struct.pack_into("<I", raw, 52 + 12, 0x20000000)  # paddr of the one segment
    bad.write_bytes(raw)
    try:
        cra.elf_flash_image(bad)
        check(False, "an ELF outside flash is refused")
    except mu.Refused:
        check(True, "an ELF outside flash is refused")

    print(f"check_release_artifacts.py: {checks} checks, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
