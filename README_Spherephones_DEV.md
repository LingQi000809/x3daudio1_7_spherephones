# Testing x3daudio1_7_spherephones in Skyrim SE via MO2

Full build-to-test walkthrough for `LingQi000809/x3daudio1_7_spherephones`, a fork of `kosumosu/x3daudio1_7_hrtf` that replaces the HRTF DSP stage with spherephones rendering.

---

## Part 1 — Prerequisites

1. **Visual Studio 2026**, Desktop development with C++ workload.
2. **DirectX SDK (June 2010)** — installs to `C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\` by default. Required because the project's `DirectX-SDK-June-2010.props` files reference it for legacy XAudio2.7/X3DAudio headers.
   - Known installer quirk on modern Windows: if setup fails with error code `S1023`, uninstall the **Visual C++ 2010 x64/x86 Redistributable** first, run the DirectX SDK installer, then reinstall the redistributable afterward. This is a well-documented conflict, not something specific to your machine.
3. **VS "MSVC v143 build tools (x86,x64)" optional component**. Check this in Part 3.
4. **Mod Organizer 2** and **Root Builder** plugin (see Part 5).
5. **Skyrim Special Edition**, already set up and launching cleanly through MO2 before you start.

---

## Part 2 — Get the source and fix the legacy SDK property sheets

1. Clone your fork:
   ```
   git clone https://github.com/LingQi000809/x3daudio1_7_spherephones.git
   cd x3daudio1_7_spherephones
   ```

2. There are four property sheets in `x3daudio1_7/` that need attention before building:
   - `DirectX-SDK-June-2010.props`
   - `DirectX-SDK-June-2010.x64.props`

3. **DirectX SDK props** — open both `DirectX-SDK-June-2010*.props` files and confirm the include/lib paths match where you actually installed the DirectX SDK June 2010 in step 1. If you installed to the default location, these likely already match and need no edit. If you installed elsewhere, fix the paths.

---

## Optional — Enable logs for debugging
Optionally, you can print sounding object coordinates to a log file to verify or debug. To do so, check out `logger::logSpatialGains` (currently commented out under `x3daudio1_7\xaudio2-hook\XAPO\SphEffect.cpp`).
If logging is included, the output log can be found under `C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\` during the gameplay.

---

## Part 3 — Configure the solution

### Visual Studio Installer
1. Go to Visual Studio Installer -> Modify -> Individual Components and install the following:
   - `MSVC v143 - VS 2022 C++ x64/x86 build tools (v14.44-17.14)` (the latest one that is not out of support)
   - `C++ v14.44(17.14) ATL for v143 build tools (x86 & x64)`


### Build Solution Configuration
1. Open `x3daudio1_7.sln` in Visual Studio.
2. In Solution Explorer, right-click the `x3daudio1_7` project → **Properties** → General, and check the **Platform Toolset**.
   - If it's set to something like `v110` or `v120` and that's not installed, either:
     - Install it via Visual Studio Installer → Modify → Individual Components → search "MSVC ... build tools", **or**
     - Right-click the project → **Retarget Projects** and let VS bump it to your installed toolset (e.g. `v143`). This is usually the simpler path and safe for this codebase.
3. Set the solution configuration dropdown in the toolbar to **Release** and the platform to **x64** (Skyrim SE is 64-bit only — do not build x86).

---

## Part 4 — Build and verify

1. Build → Build Solution (`Ctrl+Shift+B`). Watch the Output window for the first real error if it fails — with old solutions like this, the first error is usually the only one that matters; everything after it is often a cascade.
2. On success, the DLL lands at:
   ```
   x64\Release\x3daudio1_7.dll
   ```
3. **Verify exports before testing in-game.** Open "Developer Command Prompt for VS" and run:
   ```
   dumpbin /exports x64\Release\x3daudio1_7.dll
   ```
   Confirm both `X3DAudioCalculate` and `X3DAudioInitialize` appear in the export table. If either is missing, the proxy is broken and Skyrim will fail to load audio (or crash) — don't proceed to in-game testing yet.

---

## Part 5 — Install and configure Root Builder in MO2

If you don't have Mod Manager 2, download it at: https://www.modorganizer.org/. Create a new global instance and select the "Skyrim Special Edition" game to manage (of course this requires you to have this game installed from Steam). 

1. Download **Root Builder** (Kezyma's actively maintained version) from Nexus Mods (Skyrim SE Nexus, mod ID 31720) or GitHub (`Kezyma/ModOrganizer-Plugins`).
   - MO2 can only manage the `Data\` folder by default. This DLL needs to sit next to `SkyrimSE.exe` in the game root, so you need Root Builder to manage it cleanly.
2. Extract the `rootbuilder` folder (containing `__init__.py` and a `shared`/`rootbuilder` subfolder structure) directly into your **MO2 installation directory's** `plugins\` folder — this is the folder where `ModOrganizer.exe` lives, *not* your MO2 instance/profile folder. Example: `C:\Modding\MO2\plugins\rootbuilder\`.
3. Restart MO2 if it's open.
4. Click the **Tools** icon (wrench/screwdriver) in MO2's toolbar → select **Root Builder** to open its settings.
5. Ensure **Installer** is ticked — this is required for the "Root" folder convention (step 6 below) to work when installing mods.
6. Close the settings dialog. Root Builder is now active.

---

## Part 6 — Package the DLL as an MO2 mod

1. In MO2's left pane, right-click anywhere in empty space → **All Mods** → **Create empty mod**. Name it something identifiable, e.g. `Spherephones X3DAudio DLL`.
2. Right-click the new mod entry → **Open in Explorer**.
3. Inside that mod folder, create a subfolder literally named `Root`.
4. Copy your built `x3daudio1_7.dll` (from `x3daudio1_7\x64\Release\`) into that `Root` folder.

   Resulting structure:
   ```
   [MO2 instance]\mods\Spherephones X3DAudio DLL\
       Root\
           x3daudio1_7.dll
   ```
5. Back in MO2's mod list, make sure the mod's checkbox is **enabled**. It's normal for MO2 to show no entries under "Data" content for this mod — that's expected, since it only contains a Root file.
6. Root Builder will deploy `x3daudio1_7.dll` directly into your Skyrim SE install folder (next to `SkyrimSE.exe`) automatically when you launch through MO2, and will restore the original game-folder state afterward.


## Part 7 — Launch and verify in-game

1. In MO2, select your Skyrim SE launch executable (SKSE loader, typically) from the executables dropdown.
2. Launch through MO2 (not directly via Steam — that bypasses MO2's VFS and Root Builder's deployment).
3. Confirm Root Builder actually deployed the file: while Skyrim is running, check the real Skyrim SE install folder in Explorer — `x3daudio1_7.dll` should be sitting there next to `SkyrimSE.exe`.
4. Load into a save or start a new game, and get to an area with varied ambient/directional sound (a city street, a dungeon with dripping water, combat with multiple enemies — anything with several simultaneous sound sources).
