"""Protocol-level tests for the GUI reader interface (no hardware)."""

from __future__ import annotations

import io
import json
import threading

import pytest

from emrtd_pm3 import card, gui, tlv
from tests import specimens
from tests.fake_chip import FakeChip


def _jpeg(size_hint: int) -> bytes:
    """Build a real JPEG of roughly ``size_hint`` bytes (needs Pillow)."""
    PIL = pytest.importorskip("PIL.Image")
    side = 64
    data = b""
    while len(data) < size_hint and side < 2000:
        img = PIL.effect_noise((side, side), 90).convert("RGB")
        buf = io.BytesIO()
        img.save(buf, "JPEG", quality=92)
        data = buf.getvalue()
        side = int(side * 1.35)
    return data


def _dg2(jpeg: bytes) -> bytes:
    record = tlv.encode(0x02, b"\x01\x01") + tlv.encode(0x5F2E, jpeg)
    return tlv.encode(0x75, tlv.encode(0x7F61, record))


def _files(dg2: bytes | None = None) -> dict:
    files = specimens.sample_files()
    if dg2 is not None:
        files[0x0102] = dg2
    return files


class Transport:
    def __init__(self, files=None, delay: float = 0.0, on_read=None):
        self.chip = FakeChip(
            specimens.ICAO_TD3_DOC,
            specimens.ICAO_TD3_DOB,
            specimens.ICAO_TD3_EXP,
            files or specimens.sample_files(),
        )
        self.delay = delay
        self.on_read = on_read
        self.reads = 0

    def apdu(self, raw_hex, select=False):
        self.reads += 1
        if self.delay:
            import time

            time.sleep(self.delay)
        if self.on_read is not None:
            self.on_read(self.reads)
        return self.chip.apdu(raw_hex, select)


class DeferredStdin:
    """Returns a cancel request once the test signals that DG2 is being read."""

    def __init__(self):
        self.event = threading.Event()
        self.served = False

    def readline(self, *args, **kwargs):
        if self.served:
            return ""
        self.event.wait(10)
        self.served = True
        return '{"action":"cancel"}\n'

    def __iter__(self):
        return self

    def __next__(self):
        line = self.readline()
        if not line:
            raise StopIteration
        return line


def run_gui(files=None, stdin=None, delay=0.0, on_read=None):
    out = io.StringIO()
    transport = Transport(files, delay=delay, on_read=on_read)
    rc = gui.main({"mrz": specimens.ICAO_TD3_MRZ, "port": "FAKE"},
                  stdin=stdin if stdin is not None else io.StringIO(""),
                  stdout=out, transport_factory=lambda: transport)
    events = [json.loads(line) for line in out.getvalue().splitlines() if line.strip()]
    return rc, events


def result_of(events):
    return [e for e in events if e.get("event") == "result"][0]["data"]


def test_normal_dg2():
    rc, events = run_gui()
    assert rc == 0
    data = result_of(events)
    assert data["face"]["mime"] == "image/jpeg"
    assert data["files"]["DG2"] > 0


def test_large_dg2_is_transferred_completely():
    jpeg = _jpeg(20000)
    rc, events = run_gui(_files(_dg2(jpeg)))
    assert rc == 0
    data = result_of(events)
    assert data["files"]["DG2"] == len(_dg2(jpeg))
    assert len(data["face"]["data_b64"]) > 1000


def test_large_dg2_reports_receiving_progress():
    jpeg = _jpeg(20000)
    rc, events = run_gui(_files(_dg2(jpeg)))
    assert rc == 0
    receiving = [e for e in events
                 if e.get("event") == "progress" and e.get("stage") == "dg2"
                 and e.get("status") == "receiving"]
    assert receiving, "no byte-level progress during DG2"
    totals = {e["bytes_total"] for e in receiving}
    assert len(totals) == 1
    assert max(e["bytes_read"] for e in receiving) > 0


def test_dg2_sub_stages_are_emitted_in_order():
    rc, events = run_gui()
    assert rc == 0
    statuses = [e["status"] for e in events
                if e.get("event") == "progress" and e.get("stage") == "dg2"]
    for status in ("start", "receiving", "ok", "parsing", "extracting", "preview"):
        assert status in statuses, status
    assert statuses.index("parsing") > statuses.index("ok")
    assert statuses.index("preview") > statuses.index("extracting")


def test_dg2_over_15bit_limit_is_reported_not_hung():
    # 40 KiB of filler inside DG2 pushes the file beyond the READ BINARY limit.
    big = _dg2(b"\xff\xd8\xff" + b"\x00" * 40000)
    rc, events = run_gui(_files(big))
    assert rc == 0
    data = result_of(events)
    assert "DG2" not in data["files"]
    assert any("32768" in w or "15-bit" in w for w in data["warnings"]), data["warnings"]


def test_full_width_mrz_is_normalized():
    """MRZ copied from Chinese documents may use 《 instead of '<'."""
    wide = specimens.ICAO_TD3_MRZ.replace("<", "\u3008")
    out = io.StringIO()
    rc = gui.main({"action": "validate", "mrz": wide}, stdin=io.StringIO(""), stdout=out)
    events = [json.loads(line) for line in out.getvalue().splitlines() if line.strip()]
    validation = events[0]["data"]["validation"]
    assert rc == 0
    assert validation["ok"] is True, validation["issues"]
    assert validation["parsed"]["document_number"] == "L898902C3"


def test_full_width_digits_are_normalized():
    wide = specimens.ICAO_TD3_MRZ.replace("740812", "\uff17\uff14\uff10\uff18\uff11\uff12")
    out = io.StringIO()
    rc = gui.main({"action": "validate", "mrz": wide}, stdin=io.StringIO(""), stdout=out)
    events = [json.loads(line) for line in out.getvalue().splitlines() if line.strip()]
    validation = events[0]["data"]["validation"]
    assert rc == 0
    assert validation["parsed"]["date_of_birth"] == "740812"


def test_corrupt_tlv_does_not_break_the_read():
    rc, events = run_gui(_files(b"\x75\x81\xff\x00\x01"))
    assert rc == 0
    data = result_of(events)
    assert data["face"] is None


def test_unsupported_image_encoding_is_returned_as_warning():
    rc, events = run_gui(_files(_dg2(b"\x00\x01\x02\x03 not an image")))
    assert rc == 0
    data = result_of(events)
    assert data["face"]["mime"] == "application/octet-stream"
    assert data["face"]["format"] == "bin"


def test_invalid_mrz_is_rejected_before_the_device():
    out = io.StringIO()
    rc = gui.main({"mrz": "X", "port": "FAKE"}, stdin=io.StringIO(""), stdout=out,
                  transport_factory=lambda: Transport())
    events = [json.loads(line) for line in out.getvalue().splitlines() if line.strip()]
    assert rc == 4
    assert events[-1]["code"] == 4


def test_bad_json_request_reports_code_4():
    out = io.StringIO()
    rc = gui.main(None, stdin=io.StringIO("{not json\n"), stdout=out)
    events = [json.loads(line) for line in out.getvalue().splitlines() if line.strip()]
    assert rc == 4
    assert events[-1]["event"] == "error"


def test_partial_json_lines_are_never_parsed_as_events():
    """The GUI buffers until a newline; emulate arbitrary chunk boundaries."""
    _, events = run_gui()
    raw = io.StringIO()
    transport = Transport()
    gui.main({"mrz": specimens.ICAO_TD3_MRZ, "port": "FAKE"},
             stdin=io.StringIO(""), stdout=raw,
             transport_factory=lambda: transport)
    payload = raw.getvalue()
    # Split at every possible boundary and reassemble like the C++ reader.
    for cut in range(0, len(payload), 97):
        buffer = payload[:cut]
        lines = buffer.split("\n")[:-1]
        for line in lines:
            if line.strip():
                json.loads(line)  # would raise if a partial line were parsed
    assert events


def test_cancel_during_dg2_receive():
    jpeg = _jpeg(20000)
    stdin = DeferredStdin()
    transport = Transport(_files(_dg2(jpeg)))

    def on_read(count):
        if count > 6:
            stdin.event.set()

    transport.on_read = on_read
    out = io.StringIO()
    rc = gui.main({"mrz": specimens.ICAO_TD3_MRZ, "port": "FAKE"},
                  stdin=stdin, stdout=out, transport_factory=lambda: transport)
    events = [json.loads(line) for line in out.getvalue().splitlines() if line.strip()]
    assert rc == 5
    assert events[-1]["code"] == 5
    assert not any(e.get("event") == "result" for e in events)


def test_transport_error_maps_to_exit_code_3():
    from emrtd_pm3.pm3 import PM3Error

    def failing():
        raise PM3Error("no ISO14443-A card in the field")

    out = io.StringIO()
    rc = gui.main({"mrz": specimens.ICAO_TD3_MRZ, "port": "COM1"},
                  stdin=io.StringIO(""), stdout=out, transport_factory=failing)
    events = [json.loads(line) for line in out.getvalue().splitlines() if line.strip()]
    assert rc == 3
    assert events[-1]["code"] == 3


def test_read_file_reports_progress_and_rejects_oversized_files():
    class Chip(FakeChip):
        pass

    files = specimens.sample_files()
    files[0x0103] = tlv.encode(0x63, b"\x00" * 1000)
    chip = Chip(specimens.ICAO_TD3_DOC, specimens.ICAO_TD3_DOB,
                specimens.ICAO_TD3_EXP, files)
    reader = card.EMRTD(chip)
    reader.select_aid()
    reader.authenticate(specimens.ICAO_TD3_DOC, specimens.ICAO_TD3_DOB,
                        specimens.ICAO_TD3_EXP)
    seen = []
    reader.read_dg("DG3", progress=lambda read, total: seen.append((read, total)))
    assert seen and seen[-1][0] == seen[-1][1]

    files[0x0103] = tlv.encode(0x63, b"\x00" * 40000)
    chip = Chip(specimens.ICAO_TD3_DOC, specimens.ICAO_TD3_DOB,
                specimens.ICAO_TD3_EXP, files)
    reader = card.EMRTD(chip)
    reader.select_aid()
    reader.authenticate(specimens.ICAO_TD3_DOC, specimens.ICAO_TD3_DOB,
                        specimens.ICAO_TD3_EXP)
    with pytest.raises(card.EMRTDError) as info:
        reader.read_dg("DG3")
    assert "32768" in str(info.value) or "15-bit" in str(info.value)

