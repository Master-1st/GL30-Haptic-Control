"""Offline Bambu import/export check; never slices, connects, or prints."""
from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
import argparse
from pathlib import Path
from zipfile import ZipFile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_KIT = ROOT / "output/print/GL30_H2D_PETG_VALIDATION"
BAMBU = Path(r"G:\software\Bambu Studio\bambu-studio.exe")
SUPPORTED_EXTENSIONS = {"stl", "3mf"}


def _resolve_manifest(kit: Path) -> tuple[Path, str]:
    manifest_path = kit / "geometry_manifest.json"
    if manifest_path.exists():
        return manifest_path, "geometry_manifest.json"
    manifest_path = kit / "geometry_report.json"
    if manifest_path.exists():
        return manifest_path, "geometry_report.json"
    raise FileNotFoundError(f"No supported manifest found under {kit}. Expected geometry_manifest.json or geometry_report.json")


def _parse_args():
    parser = argparse.ArgumentParser(
        description="Offline Bambu import/export check for STL and 3MF files only."
    )
    parser.add_argument(
        "--kit",
        default=str(DEFAULT_KIT),
        help=f"Model kit folder to check (default: {DEFAULT_KIT})",
    )
    return parser.parse_args()


def main():
    args = _parse_args()
    kit = Path(args.kit).resolve()
    manifest_path, manifest_name = _resolve_manifest(kit)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    version = subprocess.check_output(
        ["powershell.exe", "-NoProfile", "-Command",
         "(Get-Item -LiteralPath '" + str(BAMBU) + "').VersionInfo.FileVersion"],
        text=True, creationflags=subprocess.CREATE_NO_WINDOW).strip()
    records = []
    qa_root = ROOT / "tmp/petg_bambu_qa"
    qa_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="imports_", dir=qa_root) as staging:
        for name, model in manifest["models"].items():
            for extension, info in model["files"].items():
                if extension.lower() not in SUPPORTED_EXTENSIONS:
                    continue
                source = kit / info["file"]
                assert hashlib.sha256(source.read_bytes()).hexdigest() == info["sha256"]
                work = Path(staging) / (name + "_" + extension)
                work.mkdir()
                destination = work / "import_roundtrip.3mf"
                command = [str(BAMBU), "--info", "--export-3mf", str(destination), str(source)]
                result = subprocess.run(command, cwd=work, capture_output=True, timeout=60,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
                outcome_path = work / "result.json"
                outcome = json.loads(outcome_path.read_text()) if outcome_path.exists() else {}
                assert result.returncode == 0 and outcome.get("return_code") == 0, (name, outcome, result.returncode)
                with ZipFile(destination) as archive:
                    assert archive.testzip() is None
                    config = ET.fromstring(archive.read("Metadata/model_settings.config"))
                    stats = [dict(node.attrib) for node in config.iter("mesh_stat")]
                    assert len(stats) == 1, (name, stats)
                    repairs = ("edges_fixed", "degenerate_facets", "facets_removed", "facets_reversed", "backwards_edges")
                    no_repairs = all(int(stat.get(key, -1)) == 0 for stat in stats for key in repairs)
                    assert not any(n.lower().endswith(".gcode") for n in archive.namelist())
                    assert int(stats[0]["face_count"]) > 0
                assert hashlib.sha256(source.read_bytes()).hexdigest() == info["sha256"]
                records.append({"file":info["file"], "sha256":info["sha256"],
                                "exit_code":result.returncode, "result":outcome,
                                "mesh_stats":stats, "no_reported_mesh_repairs":no_repairs,
                                "stdout_bytes":len(result.stdout), "stderr_bytes":len(result.stderr)})
                print(info["file"], "imported", "repair_free=" + str(no_repairs), flush=True)
    assert records, "No STL/3MF models checked"
    report = {"status":"BAMBU_MODEL_IMPORT_CHECK_ONLY", "bambu_executable":str(BAMBU),
              "bambu_file_version":version, "manifest":manifest_name, "kit": str(kit),
              "sliced":False, "gcode_delivered":False, "printer_contacted":False,
              "all_imported_without_reported_repairs":all(
                  row["no_reported_mesh_repairs"] for row in records), "files":records,
              "scope":"CLI read/export succeeded. Not layer preview, support validation, or physical print. Temporary Bambu projects were not delivered: their defaults are not an H2D PETG print profile."}
    (kit / "bambu_import_validation.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    assert report["all_imported_without_reported_repairs"], "Inspect reported mesh repairs before delivery"
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
