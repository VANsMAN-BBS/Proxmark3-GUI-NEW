"""Machine-readable interface for GUI front-ends (JSON in, NDJSON out).

Design goals
------------
* The GUI never needs to know anything about BAC, secure messaging or ASN.1.
* The MRZ is read from **stdin**, never from the command line, so it cannot be
  seen in a process listing.
* Nothing is written to disk.  The portrait is returned as base64 and the GUI
  decides whether to save it.
* Progress events carry stable identifiers (``stage``); the GUI supplies the
  localized text.

Wire format
-----------
stdin : one JSON object with the request, optionally followed by
        ``{"action": "cancel"}`` lines.
stdout: one JSON object per line::

    {"event": "progress", "stage": "dg1", "index": 7, "total": 12,
     "status": "start"}
    {"event": "progress", "stage": "dg1", "status": "ok"}
    {"event": "info", "message": "..."}          # no personal data
    {"event": "result", "data": {...}}
    {"event": "error", "code": 2, "stage": "bac", "message": "..."}

stderr: human diagnostics only; never the MRZ, BAC keys or file contents.

Exit codes: 0 ok, 2 card/authentication error, 3 Proxmark3 problem,
4 malformed request.
"""

from __future__ import annotations

import base64
import io
import json
import sys
import threading

from . import card as card_module
from . import dg as dg_module
from . import mrz as mrz_module
from .pm3 import PM3Error, Proxmark3

STAGES = [
    "mrz_check",
    "connect",
    "select_aid",
    "get_challenge",
    "bac",
    "ef_com",
    "dg1",
    "dg2",
    "dg11",
    "dg12",
    "parse",
    "done",
]

DEFAULT_DGS = ["DG1", "DG2", "DG11", "DG12"]

#: Upper bound for the raw hexadecimal we hand to the GUI for the "other data"
#: tab.  DG2 is deliberately excluded: only the decoded portrait is returned.
HEX_LIMIT = 32768

#: Data groups whose raw bytes are exposed as hex in the result.
HEX_DGS = ("EF_COM", "DG11", "DG12")

_SENSITIVE_KEYS = {"raw", "mrz"}


class Cancelled(Exception):
    """Raised when the GUI asked to stop a running read."""


class RequestError(Exception):
    """The request itself is malformed (bad MRZ, missing fields, ...)."""

    def __init__(self, message: str, field: str | None = None):
        super().__init__(message)
        self.field = field


def _emit(stream, payload: dict) -> None:
    stream.write(json.dumps(payload, ensure_ascii=False) + "\n")
    stream.flush()


class _Canceller:
    """Watch stdin for a cancel request while the read is running."""

    def __init__(self, stream=sys.stdin):
        self.stream = stream
        self.event = threading.Event()
        self._thread = None

    def start(self) -> None:
        if self.stream is None:
            return

        def watch() -> None:
            try:
                for line in self.stream:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        if json.loads(line).get("action") == "cancel":
                            self.event.set()
                            return
                    except (ValueError, AttributeError):
                        continue
            except (OSError, ValueError):
                return

        self._thread = threading.Thread(target=watch, name="emrtd-cancel", daemon=True)
        self._thread.start()

    def check(self) -> None:
        if self.event.is_set():
            raise Cancelled()

    @property
    def cancelled(self) -> bool:
        return self.event.is_set()


def _normalize_mrz(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n").strip("\n")


def _mrz_layout_from_text(text: str) -> str:
    lines = [line for line in text.split("\n") if line.strip()]
    if len(lines) == 3 and all(len(line) == 30 for line in lines):
        return "TD1"
    if len(lines) == 2 and all(len(line) == 36 for line in lines):
        return "TD2"
    if len(lines) == 2 and all(len(line) == 44 for line in lines):
        return "TD3"
    return "national"


def _expectations(layout: str) -> str:
    return {
        "TD1": "TD1: 3 lines x 30 characters",
        "TD2": "TD2: 2 lines x 36 characters",
        "TD3": "TD3: 2 lines x 44 characters",
    }.get(layout, "")


def validate_mrz(text: str) -> dict:
    """Parse and validate pasted MRZ text without touching the reader.

    Returns a dict with ``ok``, ``layout``, ``issues`` and the parsed fields.
    Raises :class:`RequestError` when the text cannot be interpreted at all.
    """
    text = _normalize_mrz(text)
    if not text:
        raise RequestError("MRZ is empty", field="mrz")
    layout = _mrz_layout_from_text(text)
    try:
        parsed = mrz_module.parse_mrz(text)
    except ValueError as exc:
        raise RequestError(str(exc), field="mrz") from exc

    issues: list[str] = []
    if layout in ("TD1", "TD2", "TD3"):
        expected = {"TD1": (3, 30), "TD2": (2, 36), "TD3": (2, 44)}[layout]
        lines = [line for line in text.split("\n") if line.strip()]
        if len(lines) != expected[0] or any(len(line) != expected[1] for line in lines):
            issues.append("line_length")
        for name, good in (parsed.get("checks") or {}).items():
            if not good:
                issues.append("check_digit:%s" % name)
        if not parsed.get("date_of_birth") or len(parsed.get("date_of_birth", "")) != 6:
            issues.append("date_of_birth_format")
        if not parsed.get("date_of_expiry") or len(parsed.get("date_of_expiry", "")) != 6:
            issues.append("date_of_expiry_format")
    else:
        issues.append("non_icao")

    return {
        "ok": not issues,
        "layout": parsed.get("layout", layout),
        "issues": issues,
        "parsed": parsed,
    }


def _b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def _face_payload(face: dict) -> dict:
    data = face["data"]
    out = {
        "format": face["format"],
        "container": face["container"],
        "bytes": len(data),
    }
    if face["format"] == "jpeg":
        out["mime"] = "image/jpeg"
        out["data_b64"] = _b64(data)
        return out
    if face["format"] in ("jpeg2000", "jp2"):
        try:
            from PIL import Image

            image = Image.open(io.BytesIO(data))
            image.load()
            # Preserve the original aspect ratio; no cropping or retouching.
            rgb = image.convert("RGB")
            buffer = io.BytesIO()
            rgb.save(buffer, format="PNG")
            out["mime"] = "image/png"
            out["data_b64"] = _b64(buffer.getvalue())
            out["converted_from"] = face["format"]
            return out
        except Exception as exc:  # Pillow missing or no JPEG 2000 codec
            out["warning"] = "PNG conversion failed: %s" % exc
            out["mime"] = "image/jp2"
            out["data_b64"] = _b64(data)
            return out
    out["mime"] = "application/octet-stream"
    out["data_b64"] = _b64(data)
    return out


def _hex_of(raw: bytes) -> str:
    if len(raw) > HEX_LIMIT:
        raw = raw[:HEX_LIMIT]
    return raw.hex()


def _document_view(parsed: dict) -> dict:
    """Fields for the personal-information panel (no raw MRZ repeated)."""
    keys = (
        "surname",
        "given_names",
        "name_native",
        "document_number",
        "nationality",
        "issuing_state",
        "document_code",
        "date_of_birth",
        "sex",
        "date_of_expiry",
        "personal_number",
        "note",
    )
    return {key: parsed[key] for key in keys if parsed.get(key)}


def run(request: dict, emit, transport_factory=None, cancel_check=None) -> int:
    """Execute one read request.  ``emit`` receives the event dictionaries."""
    total = len(STAGES)
    order = {name: index + 1 for index, name in enumerate(STAGES)}

    def progress(stage: str, status: str, detail: str | None = None) -> None:
        if cancel_check is not None:
            cancel_check()
        event = {
            "event": "progress",
            "stage": stage,
            "index": order[stage],
            "total": total,
            "status": status,
        }
        if detail:
            event["detail"] = detail
        emit(event)

    def info(message: str) -> None:
        emit({"event": "info", "message": message})

    # -- request ------------------------------------------------------
    mrz_text = _normalize_mrz(str(request.get("mrz") or ""))
    if not mrz_text:
        raise RequestError("No MRZ was supplied", field="mrz")

    progress("mrz_check", "start")
    checked = validate_mrz(mrz_text)
    parsed = checked["parsed"]
    doc = str(request.get("document_number") or parsed.get("document_number") or "")
    dob = str(request.get("date_of_birth") or parsed.get("date_of_birth") or "")
    exp = str(request.get("date_of_expiry") or parsed.get("date_of_expiry") or "")
    for name, value, need in (
        ("document_number", doc, 9),
        ("date_of_birth", dob, 6),
        ("date_of_expiry", exp, 6),
    ):
        if not value:
            raise RequestError("Missing %s" % name, field=name)
        if len(value) > need:
            raise RequestError("%s is too long" % name, field=name)
    progress("mrz_check", "ok", detail=checked["layout"])

    dgs = request.get("dgs") or DEFAULT_DGS
    if isinstance(dgs, str):
        dgs = [part.strip() for part in dgs.split(",") if part.strip()]
    dgs = [str(name).upper() for name in dgs]
    for name in dgs:
        if name not in card_module.FID_BY_NAME:
            raise RequestError("Unknown data group %s" % name, field="dgs")

    chunk = int(request.get("chunk") or card_module.DEFAULT_CHUNK)
    chunk = max(1, min(256, chunk))
    port = str(request.get("port") or "")
    exe = request.get("pm3_exe") or None
    incognito = bool(request.get("incognito", True))
    if not port and transport_factory is None:
        raise RequestError("No serial port was supplied", field="port")

    # -- reader -------------------------------------------------------
    result = {
        "layout": checked["layout"],
        "mrz": mrz_text,
        "document": {},
        "files": {},
        "ef_com": {},
        "warnings": list(checked["issues"]),
        "hex": {},
        "face": None,
    }
    raw: dict[str, bytes] = {}
    transport = None
    owns_transport = False
    try:
        progress("connect", "start")
        if transport_factory is not None:
            transport = transport_factory()
        else:
            transport = Proxmark3(port, exe=exe, incognito=incognito)
            transport.__enter__()
            owns_transport = True
        progress("connect", "ok")

        reader = card_module.EMRTD(transport, chunk=chunk)

        progress("select_aid", "start")
        reader.select_aid()
        progress("select_aid", "ok")

        progress("get_challenge", "start")
        challenge = reader.get_challenge(8)
        progress("get_challenge", "ok", detail="%d bytes" % len(challenge))

        progress("bac", "start")
        try:
            reader.authenticate(doc, dob, exp)
        except card_module.AuthenticationError as exc:
            progress("bac", "error")
            if exc.retries is not None:
                info("The card reports %d authentication attempts left." % exc.retries)
            raise
        progress("bac", "ok")

        progress("ef_com", "start")
        try:
            com = reader.read_ef_com()
            raw["EF_COM"] = com
            result["ef_com"] = dg_module.parse_ef_com(com)
            progress("ef_com", "ok")
        except card_module.EMRTDError as exc:
            result["warnings"].append("EF.COM: %s" % exc)
            progress("ef_com", "skip", detail=str(exc))

        for stage, name in (("dg1", "DG1"), ("dg2", "DG2"),
                            ("dg11", "DG11"), ("dg12", "DG12")):
            if name not in dgs:
                progress(stage, "skip", detail="not requested")
                continue
            progress(stage, "start")
            try:
                data = reader.read_dg(name)
            except (card_module.EMRTDError, ValueError) as exc:
                result["warnings"].append("%s: %s" % (name, exc))
                progress(stage, "skip", detail=str(exc))
                continue
            raw[name] = data
            result["files"][name] = len(data)
            progress(stage, "ok", detail="%d bytes" % len(data))

        progress("parse", "start")
        document: dict = {}
        if "DG1" in raw:
            try:
                mrz_from_chip = dg_module.parse_dg1(raw["DG1"])
                result["mrz"] = mrz_from_chip
                document = mrz_module.parse_mrz(mrz_from_chip)
            except ValueError as exc:
                result["warnings"].append("DG1: %s" % exc)
        if not document:
            document = dict(parsed)

        dg11: dict = {}
        if "DG11" in raw:
            try:
                dg11 = dg_module.parse_dg11(raw["DG11"])
                result["dg11"] = dg11
            except ValueError as exc:
                result["warnings"].append("DG11: %s" % exc)

        # Some national chips are not authoritative for names in DG1.
        if dg11.get("name_of_holder_mrz", "").count("<<") == 1:
            surname, _, given = dg11["name_of_holder_mrz"].partition("<<")
            document["surname"] = surname.replace("<", " ").strip()
            document["given_names"] = given.replace("<", " ").strip()
        if dg11.get("name_of_holder"):
            document["name_native"] = dg11["name_of_holder"]

        if "DG12" in raw:
            try:
                result["dg12"] = dg_module.parse_dg12(raw["DG12"])
            except ValueError as exc:
                result["warnings"].append("DG12: %s" % exc)

        if "DG2" in raw:
            face = dg_module.extract_face(raw["DG2"])
            if face:
                result["face"] = _face_payload(face)
                if result["face"].get("warning"):
                    result["warnings"].append(result["face"]["warning"])

        result["document"] = _document_view(document)
        result["dg1_parsed"] = {
            key: value for key, value in document.items()
            if key not in _SENSITIVE_KEYS
        }
        result["hex"] = {
            name: _hex_of(raw[name]) for name in HEX_DGS if name in raw
        }
        progress("parse", "ok")
    finally:
        if owns_transport and transport is not None:
            try:
                transport.__exit__(None, None, None)
            except Exception:
                pass

    progress("done", "ok")
    emit({"event": "result", "data": result})
    return 0


def main(request: dict | None = None, *, stdin=None, stdout=None,
         transport_factory=None) -> int:
    """Entry point used by ``python -m emrtd_pm3 gui``."""
    stdin = stdin if stdin is not None else sys.stdin
    stdout = stdout if stdout is not None else sys.stdout
    if hasattr(stdout, "reconfigure"):
        try:
            stdout.reconfigure(errors="replace")
        except (ValueError, OSError):
            pass

    if request is None:
        try:
            line = stdin.readline()
            request = json.loads(line)
        except (ValueError, OSError):
            _emit(stdout, {"event": "error", "code": 4,
                           "message": "Invalid request JSON"})
            return 4
        if not isinstance(request, dict):
            _emit(stdout, {"event": "error", "code": 4,
                           "message": "Request must be a JSON object"})
            return 4

    canceller = _Canceller(stdin)
    canceller.start()

    # Validation-only requests are answered without touching the device.
    if request.get("action") == "validate":
        try:
            checked = validate_mrz(str(request.get("mrz") or ""))
        except RequestError as exc:
            payload = {"event": "error", "code": 4, "message": str(exc)}
            if exc.field:
                payload["field"] = exc.field
            _emit(stdout, payload)
            return 4
        _emit(stdout, {"event": "result", "data": {"validation": checked}})
        return 0

    def emit(payload: dict) -> None:
        # A cancel request is checked between steps; the GUI also terminates
        # the process tree if the reader is stuck inside an APDU.
        _emit(stdout, payload)

    try:
        rc = run(request, emit, transport_factory=transport_factory,
                 cancel_check=canceller.check)
        return rc
    except Cancelled:
        _emit(stdout, {"event": "error", "code": 5,
                       "message": "Cancelled by the user"})
        return 5
    except RequestError as exc:
        payload = {"event": "error", "code": 4, "message": str(exc)}
        if exc.field:
            payload["field"] = exc.field
        _emit(stdout, payload)
        return 4
    except card_module.AuthenticationError as exc:
        payload = {"event": "error", "code": 2, "message": str(exc)}
        if exc.retries is not None:
            payload["retries"] = exc.retries
        _emit(stdout, payload)
        return 2
    except card_module.EMRTDError as exc:
        _emit(stdout, {"event": "error", "code": 2, "message": str(exc)})
        return 2
    except PM3Error as exc:
        _emit(stdout, {"event": "error", "code": 3, "message": str(exc)})
        return 3
    except Exception as exc:  # unexpected: still answer with valid JSON
        import traceback

        traceback.print_exc(file=sys.stderr)
        _emit(stdout, {"event": "error", "code": 1,
                       "message": "%s: %s" % (type(exc).__name__, exc)})
        return 1
