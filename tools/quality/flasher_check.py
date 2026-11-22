#!/usr/bin/env python3
"""Sanity checks for the self-contained browser installer page.

Verifies that docs/flasher/index.html:

* exists and is a single self-contained document
* loads no external scripts, stylesheets, images, or fonts
* keeps the element IDs the JavaScript depends on
* keeps the UF2 constants in sync with the firmware build
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PAGE = ROOT / "docs" / "flasher" / "index.html"

REQUIRED_IDS = [
    "boardStatus",
    "drop",
    "file",
    "meta",
    "mName",
    "mSize",
    "mBlocks",
    "mFamily",
    "mSha",
    "fileStatus",
    "expectSha",
    "checkSha",
    "shaStatus",
    "writeDrive",
    "driveStatus",
    "serialConnect",
    "serialDisconnect",
    "serialState",
    "console",
    "cmd",
    "send",
    "flashStatus",
    "releaseLink",
    "packLink",
]

# Resource-bearing attributes that must never point at another origin.
EXTERNAL_RESOURCE = re.compile(
    r'<(?:script|link|img|iframe|source|video|audio)\b[^>]*\b(?:src|href)\s*=\s*["\']https?://',
    re.IGNORECASE,
)

REQUIRED_MARKERS = [
    "0x0A324655",   # UF2 magic start 0
    "0x9E5D5157",   # UF2 magic start 1
    "0x0AB16F30",   # UF2 magic end
    "showDirectoryPicker",
    "navigator.serial",
    "crypto.subtle.digest",
]


def main() -> int:
    failures: list[str] = []

    if not PAGE.is_file():
        print(f"flasher_check: missing {PAGE}")
        return 1
    html = PAGE.read_text(encoding="utf-8")

    if EXTERNAL_RESOURCE.search(html):
        failures.append("page references an external resource; it must stay self-contained")

    for marker in REQUIRED_MARKERS:
        if marker not in html:
            failures.append(f"missing expected marker: {marker}")

    found_ids = set(re.findall(r'\bid\s*=\s*"([^"]+)"', html))
    for element_id in REQUIRED_IDS:
        if element_id not in found_ids:
            failures.append(f"missing element id: {element_id}")

    if html.count("<script") != html.count("</script>"):
        failures.append("unbalanced <script> tags")
    if html.count("<style") != html.count("</style>"):
        failures.append("unbalanced <style> tags")

    # Keep the documented families aligned with the root Makefile.
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    if "rp2350-arm-s" in makefile and "0xe48bff59" not in html:
        failures.append("rp2350-arm-s family id missing from the page")

    size_kb = len(html.encode("utf-8")) / 1024
    if size_kb > 200:
        failures.append(f"page is unexpectedly large ({size_kb:.0f} KiB)")

    if failures:
        print("flasher_check: FAILED")
        for item in failures:
            print(f"  - {item}")
        return 1

    print(f"flasher_check: docs/flasher/index.html ok ({size_kb:.1f} KiB, self-contained)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
