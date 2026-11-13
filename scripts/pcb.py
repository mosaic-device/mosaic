"""Check the carrier PCB and export the files JLCPCB needs to make and assemble it.

  python3 scripts/pcb.py check    # ERC, then DRC with schematic parity; fails on any violation
  python3 scripts/pcb.py export   # check, then write hardware/pcb/build/ (fab files and an assembly sheet)
"""

import csv
import html
import math
import os
import pathlib
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
PROJECT = ROOT / "hardware" / "pcb" / "mosaic-carrier"
SCH = PROJECT / "mosaic-carrier.kicad_sch"
PCB = PROJECT / "mosaic-carrier.kicad_pcb"
BUILD = ROOT / "hardware" / "pcb" / "build"

GERBER_LAYERS = "F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts"


def kicad_cli():
    candidates = [
        os.environ.get("KICAD_CLI"),
        shutil.which("kicad-cli"),
        "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli",
        r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe",
    ]
    for candidate in candidates:
        if candidate and pathlib.Path(candidate).is_file():
            return candidate
    sys.exit("kicad-cli not found; install KiCad 10 or set KICAD_CLI")


def run(*args):
    result = subprocess.run([kicad_cli(), *args], capture_output=True, text=True)
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
    return result.returncode


def check():
    BUILD.mkdir(parents=True, exist_ok=True)
    failed = False
    for name, args in (
        ("ERC", ["sch", "erc", "--severity-all", "--exit-code-violations", "-o", str(BUILD / "erc.rpt"), str(SCH)]),
        ("DRC", ["pcb", "drc", "--severity-all", "--schematic-parity", "--exit-code-violations",
                 "-o", str(BUILD / "drc.rpt"), str(PCB)]),
    ):
        report = BUILD / f"{name.lower()}.rpt"
        if run(*args) != 0:
            failed = True
            print(f"{name} failed:")
            print(report.read_text() if report.exists() else "(no report)")
        else:
            print(f"{name} passed")
    if failed:
        sys.exit(1)


def export():
    check()
    gerbers = BUILD / "gerbers"
    shutil.rmtree(gerbers, ignore_errors=True)
    gerbers.mkdir(parents=True)
    steps = [
        ["pcb", "export", "gerbers", "--layers", GERBER_LAYERS, "--subtract-soldermask", "--use-drill-file-origin", "-o", f"{gerbers}/", str(PCB)],
        ["pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
         "--generate-map", "--map-format", "gerberx2", "--drill-origin", "plot", "-o", f"{gerbers}/", str(PCB)],
        ["pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both", "--exclude-dnp", "--use-drill-file-origin",
         "-o", str(BUILD / "positions.csv"), str(PCB)],
        ["sch", "export", "bom", "--fields", "Value,Reference,Footprint,LCSC,${QUANTITY},${DNP}",
         "--labels", "Value,Reference,Footprint,LCSC,Qty,DNP", "--group-by", "Value,Footprint,LCSC,${DNP}",
         "--exclude-dnp", "-o", str(BUILD / "bom-full.csv"), str(SCH)],
        ["sch", "export", "pdf", "-o", str(BUILD / "mosaic-carrier-schematic.pdf"), str(SCH)],
    ]
    for args in steps:
        if run(*args) != 0:
            sys.exit(f"kicad-cli {' '.join(args[:3])} failed")

    archive = BUILD / "mosaic-carrier-gerbers.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(gerbers.iterdir()):
            zf.write(path, path.name)

    write_jlc_bom()
    write_jlc_cpl()
    write_assembly_sheet()
    print(f"Wrote {archive.relative_to(ROOT)}, jlc-bom.csv, jlc-cpl.csv and mosaic-carrier-assembly.html "
          f"in {BUILD.relative_to(ROOT)}")


def natural(ref):
    letters = ref.rstrip("0123456789")
    return letters, int(ref[len(letters):] or 0)


def write_assembly_sheet():
    """One page for probing and rework: the fab drawing with a reference in every part, and what each part is."""
    # Plot the fab layer from a copy of the board. Each footprint names itself twice there: a large field and
    # a small label inside the part. Move the large fields off the layer, and the small labels too where they
    # are too small to read; those parts get a large label drawn over the plot below.
    work = BUILD / "assembly"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir()
    source = PCB.read_text()
    board = re.sub(r'(\(property "Reference" "[^"]*"\s*\(at [^)]*\)\s*\(layer )"[FB]\.Fab"', r'\1"Cmts.User"', source)
    board = re.sub(r'(\(fp_text user "\$\{REFERENCE\}"\s*\(at [^)]*\)\s*\(layer )"([FB])\.Fab"(.*?\(size ([\d.]+))',
                   lambda m: f'{m.group(1)}"Cmts.User"{m.group(3)}' if m.group(2) == "B" or float(m.group(4)) < 0.8
                   else m.group(0),
                   board, flags=re.S)
    # Label each part at the middle of its pads, except the JST connectors, whose fab labels sit on their bodies.
    labels = ""
    for m in re.finditer(r'\(footprint "[^"]*"\s*\(layer "[^"]+"\)\s*\(uuid "[^"]*"\)\s*\(at ([-\d.]+) ([-\d.]+) ?([-\d.]*)\)'
                         r'(.*?)\n\t\)\n', source, flags=re.S):
        x, y, angle, body = float(m.group(1)), float(m.group(2)), math.radians(float(m.group(3) or 0)), m.group(4)
        ref = re.search(r'\(property "Reference" "([^"]+)"', body).group(1)
        value = re.search(r'\(property "Value" "([^"]*)"', body).group(1)
        if re.fullmatch(r"J[2-9]", ref):
            continue
        pads = [(float(px), float(py)) for px, py in re.findall(r'\(pad "[^"]*" \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+)', body)]
        cx = sum(px for px, _ in pads) / len(pads)
        cy = sum(py for _, py in pads) / len(pads)
        lx = x + cx * math.cos(angle) + cy * math.sin(angle) - 100   # board coordinates sit at (100, 100) on the page
        ly = y - cx * math.sin(angle) + cy * math.cos(angle) - 100
        name = f"{ref} (underside)" if ref == "J1" else ref
        labels += f'<text x="{lx:.2f}" y="{ly + 0.45:.2f}"><title>{html.escape(value)}</title>{html.escape(name)}</text>'
    shutil.copy(PROJECT / "mosaic-carrier.kicad_pro", work / "mosaic-carrier.kicad_pro")
    (work / "mosaic-carrier.kicad_pcb").write_text(board)
    svg_path = work / "drawing.svg"
    if run("pcb", "export", "svg", "--layers", "F.Fab,B.Fab,Edge.Cuts", "--mode-single", "--fit-page-to-board",
           "--exclude-drawing-sheet", "--black-and-white", "--drill-shape-opt", "1",
           "-o", str(svg_path), str(work / "mosaic-carrier.kicad_pcb")) != 0:
        sys.exit("kicad-cli pcb export svg failed")
    svg = svg_path.read_text()
    svg = svg[svg.index("<svg"):]
    svg = svg.replace("</svg>", '<g class="refs" text-anchor="middle">' + labels + "</g></svg>")

    sides = {row["Ref"]: row["Side"] for row in csv.DictReader((BUILD / "positions.csv").open(newline=""))}
    parts = []
    for row in csv.DictReader((BUILD / "bom-full.csv").open(newline="")):
        for ref in (r.strip() for r in row["Reference"].split(",")):
            parts.append((ref, row["Value"], row["Footprint"].split(":")[-1], row["LCSC"], sides.get(ref, "")))
    parts.sort(key=lambda p: natural(p[0]))
    rows = "\n".join(
        f"<tr><td>{html.escape(ref)}</td><td>{html.escape(value)}</td><td>{html.escape(fp)}</td>"
        f"<td>{html.escape(side or 'not a part')}</td><td>{html.escape(lcsc)}</td></tr>"
        for ref, value, fp, lcsc, side in parts)
    page = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Mosaic carrier assembly</title>
<style>
  body {{ font: 14px/1.4 -apple-system, system-ui, sans-serif; margin: 16px; color: #111; background: #fff; }}
  h1 {{ font-size: 20px; margin: 0 0 4px; }}
  p {{ margin: 0 0 12px; color: #444; }}
  .drawing svg {{ width: 100%; height: auto; max-width: 1100px; border: 1px solid #ccc; }}
  table {{ border-collapse: collapse; margin-top: 16px; }}
  th, td {{ border: 1px solid #ccc; padding: 4px 10px; text-align: left; }}
  th {{ background: #f2f2f2; }}
  td:first-child {{ font-weight: 600; }}
  .refs text {{ font: 700 1.3px sans-serif; fill: #c0392b; stroke: #fff; stroke-width: 0.35px; paint-order: stroke; }}
  @media print {{ body {{ margin: 8mm; }} .drawing svg {{ border: none; }} }}
</style></head><body>
<h1>Mosaic carrier: assembly</h1>
<p>Top view. J1 (the header) is on the underside. JP1 and JP2 are solder jumpers, not parts: BUF is bridged
from the factory; cut it and bridge GPIO to drive the LEDs straight from the P4.</p>
<div class="drawing">{svg}</div>
<table><thead><tr><th>Ref</th><th>Value</th><th>Footprint</th><th>Side</th><th>JLCPCB #</th></tr></thead>
<tbody>
{rows}
</tbody></table>
</body></html>
"""
    (BUILD / "mosaic-carrier-assembly.html").write_text(page)


def write_jlc_bom():
    rows = list(csv.DictReader((BUILD / "bom-full.csv").open(newline="")))
    with (BUILD / "jlc-bom.csv").open("w", newline="") as f:
        out = csv.writer(f)
        out.writerow(["Comment", "Designator", "Footprint", "JLCPCB Part #"])
        for row in rows:
            refs = [r.strip() for r in row["Reference"].split(",")]
            refs = [r for r in refs if not r.startswith("JP")]  # solder jumpers are copper, not parts
            if refs and row["LCSC"]:
                out.writerow([row["Value"], ",".join(refs), row["Footprint"].split(":")[-1], row["LCSC"]])


def write_jlc_cpl():
    rows = list(csv.DictReader((BUILD / "positions.csv").open(newline="")))
    with (BUILD / "jlc-cpl.csv").open("w", newline="") as f:
        out = csv.writer(f)
        out.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row in rows:
            ref = row["Ref"]
            if ref.startswith("JP"):
                continue
            out.writerow([ref, f"{row['PosX']}mm", f"{row['PosY']}mm", row["Side"].capitalize(), row["Rot"]])


if __name__ == "__main__":
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    if command == "check":
        check()
    elif command == "export":
        export()
    else:
        sys.exit(__doc__)
