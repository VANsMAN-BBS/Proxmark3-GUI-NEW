# Assemble the standalone eMRTD reader runtime (no Python install required).
#
# The GUI starts <dist>/emrtd/python.exe -m emrtd_pm3 gui.  This script builds
# that folder from a local CPython 3.12 installation plus the two third-party
# packages the reader needs (pycryptodome, Pillow).
#
# Usage (from the repository root):
#   powershell -ExecutionPolicy Bypass -File scripts\package-emrtd-runtime.ps1 `
#       -PythonHome "C:\Users\<user>\AppData\Local\Programs\Python\Python312" `
#       -Destination "build-runtime\emrtd"
#
# The result is a 50 MB folder.  Copy it to <dist>\Proxmark3GUI\emrtd.

param(
    [Parameter(Mandatory = $true)][string]$PythonHome,
    [string]$Destination = "build-runtime\emrtd"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo "emrtd_pm3\emrtd_pm3"
$dst = [System.IO.Path]::GetFullPath((Join-Path $repo $Destination))

if (-not (Test-Path (Join-Path $PythonHome "python.exe"))) {
    throw "python.exe not found in $PythonHome"
}
if (-not (Test-Path (Join-Path $PythonHome "Lib\site-packages\Crypto"))) {
    throw "pycryptodome is not installed in $PythonHome"
}
if (-not (Test-Path (Join-Path $PythonHome "Lib\site-packages\PIL"))) {
    throw "Pillow is not installed in $PythonHome"
}

if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
New-Item -ItemType Directory -Path $dst | Out-Null

foreach ($file in @("python.exe", "python312.dll", "python3.dll",
                    "vcruntime140.dll", "vcruntime140_1.dll", "LICENSE.txt")) {
    Copy-Item (Join-Path $PythonHome $file) (Join-Path $dst $file) -Force
}

# Standard library without tests, IDLE, tkinter and the vendored pip.
robocopy (Join-Path $PythonHome "Lib") (Join-Path $dst "Lib") /E `
    /XD site-packages test tests idlelib tkinter tcl ensurepip lib2to3 distutils __pycache__ `
    /XF *.pyc *.pdb /NFL /NDL /NJH /NJS /NP | Out-Null
robocopy (Join-Path $PythonHome "DLLs") (Join-Path $dst "DLLs") /E /XF *.pdb `
    /NFL /NDL /NJH /NJS /NP | Out-Null

foreach ($pkg in @("Crypto", "PIL")) {
    robocopy (Join-Path $PythonHome "Lib\site-packages\$pkg") `
        (Join-Path $dst "Lib\site-packages\$pkg") /E /XD __pycache__ /XF *.pyc `
        /NFL /NDL /NJH /NJS /NP | Out-Null
}

robocopy $source (Join-Path $dst "Lib\site-packages\emrtd_pm3") /MIR `
    /XD __pycache__ /XF *.pyc /NFL /NDL /NJH /NJS /NP | Out-Null

$size = (Get-ChildItem $dst -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("runtime: {0} ({1:N1} MB)" -f $dst, ($size / 1MB))

& (Join-Path $dst "python.exe") -c "import sys, Crypto, PIL, emrtd_pm3; print('python', sys.version.split()[0], '| crypto', Crypto.__version__, '| pillow', PIL.__version__)"
