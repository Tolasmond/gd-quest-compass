# Microsoft Detours source

These files are vendored from [microsoft/Detours](https://github.com/microsoft/Detours)
at commit `adb07604aa56508448b95bf037c2a6d0d3b6831a` (the upstream `main`
revision inspected September 26, 2026). The source identifies itself as Detours
4.0.1; the upstream `v4.0.1` release tag has older source, so the commit hash is
the precise revision to use for future comparisons.

`src/` contains the five C++ translation units used by `build.cmd`, their public
header `detours.h`, and `uimports.cpp`, which `creatwth.cpp` includes. `LICENSE`
is the upstream MIT license. No Detours source changes were made here.
