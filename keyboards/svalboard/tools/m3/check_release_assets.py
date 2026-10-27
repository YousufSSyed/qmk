#!/usr/bin/env python3
"""Check a downloaded release's assets (M3): check_release.sh runs this.

    check_release_assets.py DIR TAG

DIR must hold exactly svalboard_KEYMAP_TAG.uf2 for each of the 2 pair builds
(svalboard/either, one image for both halves, keymaps sval and blank: D34,
D40), from x4.2 on also svalboard_sval_left_TAG.uf2 and
svalboard_sval_right_TAG.uf2 (the one-side builds: both or neither, so older
releases still check), and nothing else: no .svup. Each pair UF2
(docs/pair-uf2.md): the pair layout (the image in whole sectors, then the
pair record; no protected flash); its manifest says TAG, hand 2 (either),
the auto build and the keymap; the image's build-info record agrees; and
make_update.py verify-pair --build release accepts it (manifest rules, hash,
structure). Each one-side file: a plain UF2 of one contiguous image in the
firmware area (no pair record), a release updater build for its side with
version TAG and the pair builds' fw_version. Exit 1 on any failure.
"""
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import check_release_artifacts as cra  # noqa: E402
import make_update as mu  # noqa: E402


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    d, tag = Path(argv[0]), argv[1]
    names = {cra.published_name(km, tag): (kb, km) for kb, km in cra.RELEASE_BUILDS}
    one_side = {cra.published_name(km, tag, side): (kb, km, side) for kb, km, side in cra.ONE_SIDE_BUILDS}
    have = {p.name for p in d.iterdir()}
    if not have & set(one_side):
        one_side = {}  # a release from before x4.2
    want = set(names) | set(one_side)
    problems = [f"unexpected file {f}" for f in sorted(have - want)] + [f"missing {f}" for f in sorted(want - have)]
    fw_versions = set()
    if not problems:
        for n, (kb, km) in sorted(names.items()):
            uf2 = d / n
            try:
                data = uf2.read_bytes()
                manifest, image = mu.parse_pair_uf2(data)
                problems += [f"{n}: {p}" for p in mu.pair_layout_problems(data)]
                m = mu.parse_manifest(manifest)
                hand, pointing_id = mu.kb_fields(kb)
                ident = dict(hand=hand, pointing_id=pointing_id, keymap_id=mu.KEYMAP[km])
                info = mu.build_info(image)
                fw_versions.add(m["fw_version"])
                if mu.manifest_version(m) != tag:
                    problems.append(f"{n}: version {mu.manifest_version(m)!r}")
                for k, v in ident.items():
                    if m[k] != v or info is None or info[k] != v:
                        problems.append(f"{n}: {k} is {m[k]} in the manifest, "
                                        f"{None if info is None else info[k]} in the image, {v} expected")
            except (mu.Refused, OSError) as e:
                problems.append(f"{n}: {e}")
                continue
            p = subprocess.run([sys.executable, "-I", str(TOOLS / "make_update.py"), "verify-pair", str(uf2), "--build", "release"], capture_output=True, text=True)
            print((p.stdout + p.stderr).strip())
            if p.returncode:
                problems.append(f"{n}: a release build refuses it")
        for n, (kb, km, side) in sorted(one_side.items()):
            try:
                image = mu.uf2_to_image((d / n).read_bytes())
                problems += [f"{n}: {p}" for p in cra.one_side_image_problems(image, kb, km, side, tag, fw_versions)]
            except (mu.Refused, OSError) as e:
                problems.append(f"{n}: {e}")
    for p in problems:
        print(f"FAIL: {p}")
    if problems:
        return 1
    print(f"ok: {tag}: {len(names)} pair UF2 files, each with the pair layout and accepted by a release build; "
          f"{len(one_side)} one-side UF2 files, each a plain release image for its side")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
