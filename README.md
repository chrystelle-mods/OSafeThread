# OSafeThread

A native SKSE (CommonLibSSE-NG) plugin that lets mods start **OStim** OStim threads without crashing even if the thread is started while one or more thread actors are in combat. It prevents the thread actors from being put into the combat state while the thread is active. 

## Build requirements

- **Windows**
- **Visual Studio 2022 Build Tools** with the C++ workload (MSVC **v143**, 14.4x) — *not* newer
  toolsets (14.5x break the pinned dependencies) and the **Windows SDK**
- **CMake** ≥ 3.21
- **Ninja** (on `PATH`)
- **vcpkg** — with the `VCPKG_ROOT` environment variable pointing at it
- **Git** (for the CommonLibSSE-NG submodule)

## Building

1. **Clone with submodules** (pulls CommonLibSSE-NG, which itself pulls `openvr`):

   ```bash
   git clone --recursive <repo-url>
   ```

   If you already cloned without `--recursive`:

   ```bash
   git submodule update --init --recursive
   ```

2. Make sure `VCPKG_ROOT` is set and Ninja is on `PATH`.

3. Configure + build (Release):

   ```bash
   cmake --preset build-release-msvc
   cmake --build --preset release-msvc
   ```

   On Windows you can instead run `build.ps1`, which enters the VS 2022 developer shell and runs the
   two commands above. Note: `build.ps1` hard-codes local tool paths (VS 2022, Ninja, vcpkg) — adjust
   them to your machine.

The build produces `OSafeThread.dll` and, via a post-build step, deploys it (plus the `data/` tree)
into a mod folder. Override the destination with `-DOUTPUT_FOLDER=<path>` at configure time.

## Papyrus

The compiled `data/Scripts/OSafeThread.pex` is committed. Recompile it from
`data/Source/Scripts/OSafeThread.psc` only when the script changes, using the Creation Kit's Papyrus
compiler (import path must include OStim's script sources for `OThreadBuilder` / `OThread`).

## Runtime

Requires **SKSE**, **Address Library**, and **OStim Standalone**. The in-game settings menu is optional
and requires **SKSE Menu Framework** (the plugin loads fine without it).
