"""Offline static instancing; run with the pinned requirements.txt environment."""

import json
from hashlib import sha256
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

root = Path(__file__).resolve().parent.parent
manifest = json.loads((root / "sources/manifest.json").read_text())
for source in manifest["sources"]:
    assert (
        sha256((root / source["file"]).read_bytes()).hexdigest() == source["sha256"]
    ), source["file"]
for family, weights in [("Manrope", [400, 500, 600]), ("Outfit", [500])]:
    for weight in weights:
        font = TTFont(root / f"sources/{family}.ttf", recalcTimestamp=False)
        font = instantiateVariableFont(font, {"wght": weight}, inplace=True)
        font.recalcTimestamp = False
        # Embed the complete tracked notice, not just upstream's license URL.
        license_text = (root / f"sources/{family}-OFL.txt").read_text()
        for record in list(font["name"].names):
            if record.nameID == 13:
                description = record.toUnicode()
                if license_text not in description:
                    description += "\n\n" + license_text
                font["name"].setName(
                    description, 13, record.platformID, record.platEncID, record.langID
                )
        # Always provide a standard Unicode English record, even if absent upstream.
        if font["name"].getName(13, 3, 1, 0x409) is None:
            font["name"].setName(license_text, 13, 3, 1, 0x409)
        # Firmware needs tabular advances independent of browser OpenType features.
        if family == "Manrope":
            cmap = font.getBestCmap()
            digits = [cmap[ord(c)] for c in "0123456789"]
            advance = max(font["hmtx"][g][0] for g in digits)
            for glyph in digits:
                font["hmtx"][glyph] = (advance, font["hmtx"][glyph][1])
        font.save(root / f"sources/{family}-{weight}.ttf")
        opts = subset.Options()
        opts.recalc_timestamp = False
        # Subsetting defaults omit copyright/license records and translations.
        opts.name_IDs = ["*"]
        opts.name_legacy = True
        opts.name_languages = ["*"]
        sub = subset.Subsetter(options=opts)
        sub.populate(
            unicodes=list(range(0x20, 0x180))
            + list(range(0x2000, 0x2070))
            + [0x20AC, 0x2212, 0xFFFD]
        )
        sub.subset(font)
        font.flavor = "woff2"
        font.save(root / f"web/{family.lower()}-{weight}.woff2")
