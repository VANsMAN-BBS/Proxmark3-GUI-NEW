"""Command line interface: ``python -m emrtd_pm3 read ...``."""

from __future__ import annotations

import argparse
import json
import os
import sys

from . import card as card_module
from . import dg as dg_module
from . import mrz as mrz_module
from .pm3 import PM3Error, Proxmark3, list_proxmark_ports

DEFAULT_DGS = "DG1,DG2,DG11,DG12"


def _port(args) -> str:
    if args.port:
        return args.port
    ports = list_proxmark_ports()
    if len(ports) == 1:
        return ports[0]
    raise SystemExit(
        "cannot determine the Proxmark3 port; pass --port (found: %s)"
        % (", ".join(ports) if ports else "none")
    )


def _proxmark(args) -> Proxmark3:
    return Proxmark3(_port(args), exe=args.pm3_exe,
                     incognito=not getattr(args, "keep_logs", False))


def cmd_selftest(args) -> int:
    with _proxmark(args) as pm3:
        card = card_module.EMRTD(pm3)
        card.select_aid()
        challenge = card.get_challenge(8)
        print("[+] eMRTD AID selected")
        print("[+] GET CHALLENGE -> %s (%d bytes)" % (challenge.hex(), len(challenge)))
    return 0


def cmd_gui(args) -> int:
    """Machine-readable mode used by the Proxmark3GUI front-end."""
    from . import gui as gui_module

    return gui_module.main()


def cmd_read(args) -> int:
    names = [name.strip().upper() for name in args.dgs.split(",") if name.strip()]
    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)

    summary: dict = {"files": {}, "warnings": []}

    with _proxmark(args) as pm3:
        card = card_module.EMRTD(pm3, chunk=args.chunk)
        card.select_aid()
        try:
            card.authenticate(args.doc, args.dob, args.exp)
        except card_module.AuthenticationError as exc:
            print("[!] %s" % exc, file=sys.stderr)
            if exc.retries is not None:
                print("[!] attempts left according to the card: %d" % exc.retries,
                      file=sys.stderr)
            return 2
        print("[+] BAC authentication OK")

        raw: dict[str, bytes] = {}
        try:
            com = card.read_ef_com()
            raw["EF_COM"] = com
            summary["ef_com"] = dg_module.parse_ef_com(com)
            print("[+] EF.COM %s" % summary["ef_com"])
        except card_module.EMRTDError as exc:
            summary["warnings"].append("EF.COM: %s" % exc)

        for name in names:
            try:
                data = card.read_dg(name)
            except (card_module.EMRTDError, ValueError) as exc:
                summary["warnings"].append("%s: %s" % (name, exc))
                print("[!] %s not read: %s" % (name, exc), file=sys.stderr)
                continue
            raw[name] = data
            summary["files"][name] = len(data)
            with open(os.path.join(out_dir, name + ".bin"), "wb") as fh:
                fh.write(data)
            print("[+] %-5s %6d bytes" % (name, len(data)))

        document: dict = {}
        if "DG1" in raw:
            try:
                mrz_text = dg_module.parse_dg1(raw["DG1"])
                summary["mrz"] = mrz_text
                document = mrz_module.parse_mrz(mrz_text)
            except ValueError as exc:
                summary["warnings"].append("DG1: %s" % exc)

        dg11: dict = {}
        if "DG11" in raw:
            try:
                dg11 = dg_module.parse_dg11(raw["DG11"])
                summary["dg11"] = dg11
            except ValueError as exc:
                summary["warnings"].append("DG11: %s" % exc)

        # The MRZ in some national chips is not authoritative for names.
        if dg11.get("name_of_holder_mrz", "").count("<<") == 1:
            surname, _, given = dg11["name_of_holder_mrz"].partition("<<")
            document["surname"] = surname.replace("<", " ").strip()
            document["given_names"] = given.replace("<", " ").strip()
        if dg11.get("name_of_holder"):
            document["name_native"] = dg11["name_of_holder"]

        if "DG12" in raw:
            summary["dg12"] = dg_module.parse_dg12(raw["DG12"])

        summary["document"] = document
        if document:
            print("\n[i] Document")
            for key in ("surname", "given_names", "name_native", "document_number",
                        "nationality", "date_of_birth", "sex", "date_of_expiry"):
                if document.get(key):
                    print("    %-16s %s" % (key, document[key]))

        if "DG2" in raw:
            face = dg_module.extract_face(raw["DG2"])
            if face:
                ext = {"jpeg": "jpg", "jpeg2000": "j2k", "jp2": "jp2"}.get(
                    face["format"], "bin")
                path = os.path.join(out_dir, "face." + ext)
                with open(path, "wb") as fh:
                    fh.write(face["data"])
                summary["face"] = {
                    "format": face["format"],
                    "container": face["container"],
                    "bytes": len(face["data"]),
                    "path": path,
                }
                print("[+] face image -> %s (%d bytes)" % (path, len(face["data"])))
                if not args.no_photo_png and face["format"] in ("jpeg2000", "jp2"):
                    try:
                        from PIL import Image
                        png = os.path.join(out_dir, "face.png")
                        Image.open(path).convert("RGB").save(png)
                        summary["face"]["png"] = png
                        print("[+] face preview -> %s" % png)
                    except Exception as exc:  # Pillow missing or codec unavailable
                        summary["warnings"].append("png conversion: %s" % exc)

    with open(os.path.join(out_dir, "summary.json"), "w", encoding="utf-8") as fh:
        json.dump(summary, fh, ensure_ascii=False, indent=2)
    print("[+] saved to %s" % out_dir)

    if args.json:
        print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="emrtd_pm3",
        description="Read ICAO 9303 travel documents through a Proxmark3 (BAC).",
    )
    parser.add_argument("--pm3-exe", help="path to proxmark3.exe (or set PM3_EXE)")
    parser.add_argument("--keep-logs", action="store_true",
                        help="let the PM3 client persist history/logs (off by default)")
    sub = parser.add_subparsers(dest="command", required=True)

    selftest = sub.add_parser("selftest", help="select the eMRTD app and get a challenge")
    selftest.add_argument("--port", help="serial port, e.g. COM14")
    selftest.set_defaults(func=cmd_selftest)

    read = sub.add_parser("read", help="authenticate and read data groups")
    read.add_argument("--port", help="serial port, e.g. COM14")
    read.add_argument("--doc", required=True, help="document number (MRZ)")
    read.add_argument("--dob", required=True, help="date of birth, YYMMDD")
    read.add_argument("--exp", required=True, help="expiry date, YYMMDD")
    read.add_argument("--out", default="emrtd_out", help="output directory")
    read.add_argument("--dgs", default=DEFAULT_DGS, help="comma separated data groups")
    read.add_argument("--chunk", type=int, default=card_module.DEFAULT_CHUNK,
                      help="READ BINARY chunk size (1..256, default 224)")
    read.add_argument("--no-photo-png", action="store_true",
                      help="do not convert JPEG2000 portraits to PNG")
    read.add_argument("--json", action="store_true", help="also print summary JSON")
    read.set_defaults(func=cmd_read)

    gui = sub.add_parser(
        "gui",
        help="machine-readable mode for GUI front-ends (JSON on stdin, NDJSON on stdout)",
    )
    gui.set_defaults(func=cmd_gui)
    return parser


def main(argv: list[str] | None = None) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except PM3Error as exc:
        print("[!] Proxmark3 error: %s" % exc, file=sys.stderr)
        return 3
    except card_module.EMRTDError as exc:
        print("[!] card error: %s" % exc, file=sys.stderr)
        return 2
