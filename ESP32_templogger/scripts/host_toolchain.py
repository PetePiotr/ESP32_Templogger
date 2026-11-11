# =============================================================================
#  host_toolchain.py  –  Host-Compiler für env:native (Tests auf dem PC)
# -----------------------------------------------------------------------------
#  PlatformIO ruft für "platform = native" einfach gcc/g++ aus dem PATH auf.
#  Ist keiner im PATH, wird hier ein bekannter MinGW-Ordner vorangestellt.
#  -static: Die Testprogramme laufen dann ohne MinGW-DLLs im PATH.
# =============================================================================
Import("env")  # noqa: F821  (von PlatformIO/SCons bereitgestellt)
import os
import shutil

CANDIDATES = [
    r"C:\Program Files\CodeBlocks\MinGW\bin",
    r"C:\msys64\ucrt64\bin",
    r"C:\msys64\mingw64\bin",
    r"C:\mingw64\bin",
]

if not shutil.which("g++"):
    for path in CANDIDATES:
        if os.path.isfile(os.path.join(path, "g++.exe")):
            env.PrependENVPath("PATH", path)  # noqa: F821
            print("host_toolchain: verwende", path)
            break
    else:
        print("host_toolchain: WARNUNG – kein g++ gefunden (MinGW installieren oder PATH setzen)")

if os.name == "nt":
    env.Append(LINKFLAGS=["-static"])  # noqa: F821
