"""Thin driver around the Proxmark3 client, used as a plain ISO 14443-4 reader.

Why a subprocess and not a library?  The iceman client already speaks all the
hardware details; ``hf 14a apdu`` sends one ISO 7816 APDU and keeps the RF
field on between calls (``-k``), which is exactly what BAC needs.  Keeping one
long-lived client process is essential: dropping the field resets the card and
destroys the authenticated session.

The client is started with ``-f`` so stdout is flushed after every print; the
prompt itself is not newline-terminated, so commands are considered finished
when the output has been quiet for a short moment.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
import threading
import time

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
RESPONSE = re.compile(r"<<<\s*([0-9A-Fa-f]+)")


class PM3Error(Exception):
    pass


def list_proxmark_ports() -> list[str]:
    """Best-effort port discovery; requires pyserial (optional dependency)."""
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    found = []
    for port in list_ports.comports():
        text = (port.description or "").lower()
        if "proxmark" in text or port.vid == 0x9AC4:
            found.append(port.device)
    return found


def find_client(explicit: str | None = None) -> str:
    if explicit:
        return explicit
    env = os.environ.get("PM3_EXE")
    if env:
        return env
    found = shutil.which("proxmark3")
    if found:
        return found
    raise PM3Error("proxmark3 client not found; pass --pm3-exe or set PM3_EXE")


class Proxmark3:
    """One long-lived ``proxmark3`` client process."""

    def __init__(self, port: str, exe: str | None = None, quiet: float = 0.35,
                 start_timeout: float = 15.0, incognito: bool = True):
        self.port = port
        self.exe = find_client(exe)
        self.quiet = quiet
        self.lines: list[str] = []
        self._lock = threading.Lock()

        args = [self.exe, "-f", port]
        if incognito:
            # do not persist history/preferences/log files (they may contain
            # APDU transcripts from a personal document)
            args.append("--incognito")

        # The client writes .proxmark3/ next to the working directory before it
        # parses --incognito; run it in a throw-away directory and delete it.
        self._workdir = tempfile.mkdtemp(prefix="emrtd_pm3_")

        self.proc = subprocess.Popen(
            args,
            cwd=self._workdir,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        threading.Thread(target=self._read, daemon=True).start()
        if not self._settle(start_timeout, min_lines=1):
            raise PM3Error("Proxmark3 client produced no output on %s" % port)

    # -- context manager ----------------------------------------------

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- plumbing ------------------------------------------------------

    def _read(self) -> None:
        for line in self.proc.stdout:
            with self._lock:
                self.lines.append(line)

    def _settle(self, timeout: float, min_lines: int = 0) -> bool:
        last = -1
        stable = 0.0
        deadline = time.time() + timeout
        while time.time() < deadline:
            time.sleep(0.1)
            with self._lock:
                count = len(self.lines)
            if count != last:
                last = count
                stable = 0.0
            else:
                stable += 0.1
                if count >= min_lines and stable >= self.quiet:
                    return True
        return False

    def _settle_response(self, timeout: float, start: int) -> bool:
        """Wait for an APDU ``<<<`` line (or an error) instead of going quiet.

        ``hf 14a apdu`` prints exactly one ``<<< hex`` line per command, so the
        fixed 350 ms quiet window is unnecessary here.  Waiting for the marker
        removes ~0.35 s from *every* APDU, which dominates large DG2 reads.
        """
        deadline = time.time() + timeout
        grace_deadline = None
        while time.time() < deadline:
            time.sleep(0.02)
            with self._lock:
                new_lines = self.lines[start:]
            if grace_deadline is None:
                for line in new_lines:
                    if RESPONSE.search(line) or "[!]" in line or "[-]" in line:
                        # give the client a moment to finish the trailing prompt
                        grace_deadline = time.time() + 0.06
                        break
            elif time.time() >= grace_deadline:
                return True
        return bool(new_lines)

    def _tail(self, limit: int = 400) -> str:
        """Last part of the client output, for actionable error messages."""
        with self._lock:
            text = "".join(self.lines).strip()
        return text[-limit:]

    def command(self, command: str, timeout: float = 30.0,
                response_mode: bool = False) -> str:
        if self.proc.poll() is not None:
            raise PM3Error(
                "Proxmark3 client exited (code %s) before '%s' could be sent: %s"
                % (self.proc.returncode, command.split(" ")[0], self._tail() or "no output")
            )
        with self._lock:
            start = len(self.lines)
        try:
            self.proc.stdin.write(command + "\n")
            self.proc.stdin.flush()
        except OSError as exc:
            raise PM3Error(
                "cannot talk to the Proxmark3 client: %s (%s)"
                % (exc, self._tail() or "no output")
            ) from exc
        if response_mode:
            self._settle_response(timeout, start)
        else:
            time.sleep(0.05)
            self._settle(timeout, min_lines=start + 1)
        with self._lock:
            chunk = "".join(self.lines[start:])
        return ANSI.sub("", chunk)

    # -- APDU ----------------------------------------------------------

    def apdu(self, raw_hex: str, select: bool = False, timeout: float = 30.0) -> bytes:
        """Send a raw APDU; returns the response data followed by SW1 SW2.

        ``select=True`` adds ``-s`` (activate field and select card).  Every
        call keeps the field on (``-k``) so the card session survives.
        """
        flags = "-sk" if select else "-k"
        output = self.command("hf 14a apdu %s -d %s" % (flags, raw_hex), timeout,
                              response_mode=True)

        response = None
        for line in output.splitlines():
            if "<<<" not in line or "status:" in line:
                continue
            match = RESPONSE.search(line)
            if match:
                response = match.group(1)
        if response is None:
            if "No ISO14443-A Card in field" in output:
                raise PM3Error(
                    "no ISO14443-A card in the field; place the document on the antenna"
                )
            raise PM3Error("no APDU response for %s:\n%s" % (raw_hex, output))

        data = bytes.fromhex(response)
        if len(data) < 2:
            raise PM3Error("short APDU response: %s" % response)
        return data

    def close(self) -> None:
        try:
            self.proc.stdin.write("quit\n")
            self.proc.stdin.flush()
            time.sleep(0.4)
        except Exception:
            pass
        try:
            self.proc.kill()
        except Exception:
            pass
        shutil.rmtree(self._workdir, ignore_errors=True)
