"""Gathers the licences of what a release binary contains, for its package.

    python tools/licenses.py build/windows-msvc-release/vcpkg_installed > THIRD_PARTY_LICENSES.txt

The binaries link teta-wot-cpp's vcpkg dependencies (the overlay port's:
Drogon, Boost.PFR, nlohmann/json, RE2, tl-expected) and everything those
depend on, for the target triplet. This reads vcpkg's installed status
(`<installed>/vcpkg/status`), follows the dependencies from those roots,
and writes each package's copyright file (`<installed>/<triplet>/share/<package>/copyright`),
after teta-wot-cpp's own licence and THIRD_PARTY_NOTICES.md. Host tools
(`vcpkg-cmake` and the like, `:x64-windows` in the status) aren't linked, and
are left out. A summary of the licence kinds goes to standard error.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROOTS = ["boost-pfr", "drogon", "nlohmann-json", "re2", "tl-expected"]
# A licence kind, by a phrase its text has.
KINDS = {
    "MIT": "Permission is hereby granted, free of charge",
    "Apache-2.0": "Apache License",
    "BSD": "Redistribution and use in source and binary forms",
    "BSL-1.0": "Boost Software License",
    "CC0": "CC0",
    "Zlib": "This software is provided 'as-is'",
    "public domain": "public domain",
}


def packages(status):
    """The installed packages: name -> (version, dependencies), for one triplet."""
    found = {}
    for paragraph in status.read_text(encoding="utf-8").split("\n\n"):
        fields = dict(re.findall(r"^([A-Za-z-]+): (.*)$", paragraph, re.M))
        if "Package" not in fields or "not-installed" in fields.get("Status", ""):
            continue
        name = fields["Package"]
        triplet = fields.get("Architecture", "")
        depends = [d.strip() for d in fields.get("Depends", "").split(",") if d.strip()]
        entry = found.setdefault((name, triplet), {"version": None, "depends": set()})
        if "Feature" not in fields:
            entry["version"] = fields.get("Version")
        entry["depends"].update(depends)
    return found


def closure(found, triplet):
    """The packages the roots need for `triplet`, without host tools."""
    seen = []
    queue = list(ROOTS)
    while queue:
        name = queue.pop(0)
        if name in seen or (name, triplet) not in found:
            continue
        seen.append(name)
        for depend in found[(name, triplet)]["depends"]:
            if ":" in depend:  # a host tool, such as vcpkg-cmake:x64-windows
                continue
            queue.append(depend.split("[")[0])
    return sorted(seen)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("installed", type=Path, help="the vcpkg_installed folder of the release build")
    parser.add_argument("--triplet", default="x64-windows-static")
    args = parser.parse_args()
    found = packages(args.installed / "vcpkg" / "status")
    names = closure(found, args.triplet)
    out = sys.stdout
    out.reconfigure(encoding="utf-8", newline="\n")
    out.write("Licences of teta-wot-cpp and of what its binaries contain\n" + "=" * 58 + "\n\n")
    out.write("teta-wot-cpp\n" + "-" * 12 + "\n\n" + (ROOT / "LICENSE").read_text(encoding="utf-8") + "\n")
    out.write((ROOT / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8") + "\n")
    summary = []
    for name in names:
        copyright_file = args.installed / args.triplet / "share" / name / "copyright"
        text = copyright_file.read_text(encoding="utf-8", errors="replace") if copyright_file.exists() else ""
        version = found[(name, args.triplet)]["version"]
        title = f"{name} {version}"
        out.write(f"\n{title}\n{'-' * len(title)}\n\n{text.strip() or '(no copyright file)'}\n")
        kinds = [kind for kind, phrase in KINDS.items() if phrase.lower() in text.lower()]
        summary.append(f"{name:<24} {', '.join(kinds) or ('no licence text' if not text else 'UNKNOWN: read it')}")
    print(f"{len(names)} packages:", *summary, sep="\n  ", file=sys.stderr)


if __name__ == "__main__":
    main()
