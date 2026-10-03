# Building CvGameCoreDLL.dll

Everything the build needs is in this repository except three Microsoft components. Their licenses do
not allow redistributing them, so they have to be installed (or copied in) once per machine.

| Component | Used for | Expected location |
|---|---|---|
| **Visual C++ Toolkit 2003** | `cl.exe`, `link.exe`, the C/C++ headers and runtime libraries | `C:\Program Files (x86)\Microsoft Visual C++ Toolkit 2003` |
| **Old Windows SDK** (Platform SDK headers/libraries that work with the 2003 compiler) | `windows.h` and the other Windows headers (`include`, `include\mfc`), `winmm.lib` / `user32.lib` (`lib`), `rc.exe` + `RcDll.Dll` (`bin`) | `Tools\WindowsSDK\include`, `Tools\WindowsSDK\lib`, `Tools\WindowsSDK\bin` (git-ignored) |
| **Visual Studio 2017 or newer** with "Desktop development with C++" | only `nmake.exe` (found via `vswhere`, or anywhere on `PATH`) | any |

The Windows SDK files are the ones the Civ4 SDK installer from the modding community puts into
`Civ4SDK\WindowsSDK`. Copy its `Include`, `Lib` and `Bin` folders into `Tools\WindowsSDK`, or point `PSDK` in
`CvGameCoreDLL\Makefile.settings` at an installed copy. Also needed, but part of Windows: `cvtres.exe` from the
.NET Framework 4 (`%SystemRoot%\Microsoft.NET\Framework\v4.0.30319`).

Everything else is in the repository: the BtS 3.19 SDK sources, Boost 1.32 and Python 2.4 headers and libraries
(`CvGameCoreDLL\Boost-1.32.0`, `CvGameCoreDLL\Python24`), `jom.exe` / `fastdep.exe` (`CvGameCoreDLL\bin`) and the
`sal.h` stub for the SDK headers (`Tools\compat`).

## Build

```
CvGameCoreDLL\build.bat            Release build (also copied to Assets\CvGameCoreDLL.dll)
CvGameCoreDLL\build.bat Timing     Release + profiler (see performance_doc.md, if present)
CvGameCoreDLL\build.bat Release clean
```

`build.bat` checks for the three components first and stops with a message naming the missing one. The full
log is written to `CvGameCoreDLL\build.log`.
