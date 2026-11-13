"""Check the carrier PCB and export the files JLCPCB needs to make and assemble it.

  python3 scripts/pcb.py check    # ERC, then DRC with schematic parity; fails on any violation
  python3 scripts/pcb.py export   # check, then write hardware/pcb/build/
"""

import csv
import os
import pathlib
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
    print(f"Wrote {archive.relative_to(ROOT)}, jlc-bom.csv and jlc-cpl.csv in {BUILD.relative_to(ROOT)}")


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
