# ps-rescue-app — VitaBootDiag

Boot rescue & diagnostics for hacked PS Vita / PS TV (HENkaku / Enso + taiHEN).

* **VitaBootDiag.vpk** (~100 KB) — GUI app: console/3G/SD2Vita detector, `config.txt` analyser
  and safe patcher, crash-dump cleaner, offline error dictionary, boot-log viewer, report export.
* **boot_logger.skprx** — kernel plugin: boot counter + "did the previous boot reach SceShell?"
  + kernel plugin list + mount state 15 s after boot.
* **boot_logger.suprx** — SceShell plugin: marks the boot as complete, logs your `*main` plugins
  and ux0 free space.

No GPU libs, no fonts on disk, no network. Software framebuffer with an embedded font, redraws only
on input — zero CPU work while idle. Plugins install no hooks; the kernel one runs one short thread
that deletes itself after 15 s.

## Layout

```
ps-rescue-app/
├── CMakeLists.txt                 top level (plugins + app)
├── common/bootdiag_shared.h       state file format + paths shared by all three binaries
├── plugins/
│   ├── boot_logger_kernel/        boot_logger.skprx  (main.c, exports.yml, CMakeLists.txt)
│   └── boot_logger_user/          boot_logger.suprx  (main.c, exports.yml, CMakeLists.txt)
├── app/
│   ├── CMakeLists.txt             VitaBootDiag.vpk (title ID VBDG00001, unsafe)
│   ├── sce_sys/                   icon + LiveArea
│   └── src/
│       ├── main.c                 UI / input / screens
│       ├── gfx.c                  framebuffer renderer
│       ├── font_data.h            generated 10x20 font (DejaVu Sans Mono)
│       ├── fsutil.c               ALL writes go through fs_write_allowed() (ur0:/ux0: only)
│       ├── config.c               taiHEN config.txt parser + patcher (backup before every save)
│       ├── detect.c               model / 3G / SD2Vita drivers / mounts / crash dumps
│       ├── errdict.c              offline error + symptom dictionary
│       ├── fixes.c                Fix 1-5 + boot logger install/uninstall
│       └── report.c               Square-button text report
├── tools/gen_font.py, gen_assets.py
└── .github/workflows/build.yml    CI build in the vitasdk Docker image
```

## Build

Requires VitaSDK (`$VITASDK` set, `$VITASDK/bin` on `PATH`) and CMake ≥ 3.16. No extra vdpm packages.

```sh
git clone https://github.com/<you>/ps-rescue-app && cd ps-rescue-app
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Outputs:

```
build/app/VitaBootDiag.vpk
build/plugins/boot_logger_kernel/boot_logger.skprx
build/plugins/boot_logger_user/boot_logger.suprx
```

Both plugins are also packed into the VPK (`app0:plugins/`), so the app can install them itself.

Optional regeneration of generated files (Python 3 + Pillow): `python3 tools/gen_font.py` and
`python3 tools/gen_assets.py`.

## Install

1. Install `VitaBootDiag.vpk` with VitaShell (it's an unsafe app — needed to read/write `ur0:tai`).
2. Boot logger, either:
   * **Automatic:** app → *Fixes* → *Install boot logger plugins*, then reboot, or
   * **Manual:** copy both plugins to `ur0:tai/` and edit `ur0:tai/config.txt`:

```
*KERNEL
ur0:tai/boot_logger.skprx        <- FIRST line under *KERNEL
ur0:tai/storagemgr.skprx         (your existing kernel plugins stay below it)
...

*main
ur0:tai/henkaku.suprx
...                              (your existing SceShell plugins)
ur0:tai/boot_logger.suprx        <- LAST line under *main
```

Order matters: the kernel logger must run before anything that could crash, and the shell logger
must run after every other `*main` plugin. If a boot dies between the two, the next boot's log
says **PREVIOUS BOOT DIED BETWEEN KERNEL STAGE AND SCESHELL** and the app shows it on *Overview*.

Logs: `ur0:data/VitaBootDiag/boot.log` (rotated to `boot.log.old` above 48 KB) and `state.bin`.

## Controls

| Button | Action |
|---|---|
| D-Pad / left stick | navigate (left/right = page in lists) |
| L / R | switch tab (Overview, Plugins, Fixes, Dumps, Errors) |
| X | select / apply fix |
| O | cancel / back |
| Triangle | raw boot log viewer |
| Square | export report → `ux0:data/VitaBootDiag/report_<time>.txt` (ur0 if ux0 is not mounted) |
| START | exit |

## Fixes

| # | What it does | Safety |
|---|---|---|
| 1 | `ux0:tai/config.txt` exists next to `ur0:tai/config.txt` → rename `ux0:tai` → `ux0:tai_backup` | rename, never delete; refused if ur0 config is missing |
| 2 | comment out entries whose file is missing, `.skprx` outside `*KERNEL` / `.suprx` inside it, or entries with no section / no device | lines on currently unmounted devices are left alone; backup first |
| 3 | more than one of StorageMgr / YAMT / gamesd / usbmc enabled → you choose which to keep, others' lines get commented out | backup first |
| 4 | delete `*.psp2dmp` in `ux0:data` and `ur0:data` | — |
| 5a | cold reboot (`scePowerRequestColdReset`) | — |
| 5b | rename `ux0:id.dat` → card re-indexed on next boot | rename, backup kept |
| 5c | rename `ur0:shell/db/app.db` → LiveArea DB rebuilt on next boot (loses folders/layout) | rename, backup kept |

Every `config.txt` save writes `ur0:tai/config.txt.vbd_<timestamp>.bak` first, writes a temp file,
verifies its size, then swaps it in (and restores the backup if the swap fails).

**Write protection:** `fs_write_allowed()` rejects any write/rename/delete whose path is not on
`ur0:` or `ux0:` or contains `..`. The plugins write only to `ur0:data/VitaBootDiag/`.

## If the console no longer boots

On Enso, hold **SELECT** while powering on: the optional (`-`) entries of `ur0:tai/boot_config.txt`
are skipped, which normally means taiHEN plugins are not loaded. Then open VitaBootDiag, press
Triangle for the log and use Fix 2 / Fix 3, or restore a `config.txt.vbd_*.bak`.

## Notes / limits

* Model detection: PS TV via `sceKernelIsPSVitaTV`, Slim vs OLED via presence of `imc0:`,
  3G via `vshSysconHasWWAN`. Firmware shown is the one reported to apps (can be spoofed).
* YAMT setups that load YAMT through `ur0:tai/boot_config.txt` instead of `config.txt` are not
  touched by Fix 3 (only `config.txt` is edited).
* The error dictionary covers SCE errno codes, taiHEN return codes, C2-12828-1 and common boot
  symptoms.
