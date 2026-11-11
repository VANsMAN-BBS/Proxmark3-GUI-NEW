"""PyInstaller entry point for the standalone eMRTD reader."""

from emrtd_pm3.cli import main

if __name__ == "__main__":
    raise SystemExit(main())
