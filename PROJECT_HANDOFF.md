# OpenHantek6022 macOS Project Handoff

## Purpose

This repository is the user's macOS-focused fork of OpenHantek6022. The current
target is an Intel-only Mac. The user does not want to install or maintain a
local compiler toolchain; builds and DMG packaging are performed by GitHub
Actions.

All hardware-related changes must be built through GitHub Actions and tested
with the physical oscilloscope before they are merged into `main`.

## Repository

- User repository: <https://github.com/TempAB/MacOs-OpenHantek6022.git>
- Original project: <https://github.com/OpenHantek/OpenHantek6022.git>
- `origin`: the user's repository
- `openhantek`: the original project
- Final production implementation: `b52c0ea`
- Final production branch: `codex/eeprom-calibration-final`

The final production branch was created directly from `origin/main`. It contains
the hardware-validated null-window and identical-candidate safeguards without
the temporary repeatability-study interface or collection code. After the
public-facing README attribution was added, this branch was fast-forwarded into
`main`.

## Build Process

- GitHub Actions workflow: `.github/workflows/build.yml`
- Target artifact: self-contained Intel `x86_64` macOS DMG
- Do not install a local compiler toolchain unless the user explicitly requests
  it.
- Do not merge hardware-related changes until the user has tested the build.

## Completed and Hardware-Tested Work

### Intel macOS packaging

The GitHub Actions workflow builds a self-contained Intel macOS application and
DMG. Relevant commits include:

- `40d59e8` — build self-contained Intel macOS DMG
- `e0e3c8e` — avoid rebundling Qt frameworks
- `495f801` — audit Mach-O load commands accurately

### Selector behavior

Selector behavior was standardized while retaining `SiSpinBox` for engineering
values:

- Mouse-wheel changes work while hovering.
- Up/down keyboard selection works.
- Native click-to-open behavior is preserved.
- Custom timebase, samplerate, and frequency controls retain their intended
  numeric behavior.

Relevant commit: `1f13894`.

### Offset calibration

Offset calibration now:

- Requires both channels and a 10–100 kS/s sample rate.
- Discards the first frame after a range change.
- Requires two stable, valid frames for each measurement.
- Rejects clipped, out-of-range, or unstable measurements.
- Requires all 16 channel/range combinations.
- Cancels an incomplete calibration without changing the INI.
- Saves and verifies a completed calibration immediately.
- Creates an INI backup before replacement and restores it on failure.

Relevant commits: `75ba280` and `0311d21`.

### Native macOS calibration storage

The active device-specific calibration file is stored under:

```text
~/Library/Application Support/OpenHantek/OpenHantek6022/Calibration
```

The application provides **Oscilloscope → Show Calibration Folder**. Legacy
calibration files are copied and verified during migration while the original
is retained.

Relevant commits: `b533288` and `4626a19`.

### Guarded EEPROM calibration

Normal offset calibration never writes the EEPROM. It saves residual
corrections to the INI.

**Oscilloscope → EEPROM Calibration Safety** provides:

- A read-only dry run by default.
- Two matching reads of the complete 80-byte calibration region.
- Exact EEPROM and INI backups.
- Candidate images, readable reports, and SHA-256 manifests.
- An advanced EEPROM-update checkbox that is unchecked every time and never
  remembered.
- A second explicit confirmation before a physical write.
- Writes restricted to four aligned 8-byte low-speed offset chunks.
- Complete 80-byte readback verification.
- Automatic restoration of the original chunks and another readback when a
  write, readback, INI, or audit step fails.
- Persistent transaction-state and result records.
- INI reconciliation after success so low-speed corrections are not applied
  twice.

Relevant commits: `08f99a2` and `521a1a9`.

### Null-window and no-material-change guard

The final EEPROM candidate preparation adds:

- An adjustable zero-centred null half-width from `0.00` through `0.50` ADC
  count in `0.01` increments.
- A hardware-validated default of `0.30` that resets every time the dialog
  opens and is never persisted.
- Inclusive filtering: for an enabled window, a low-speed INI residual is
  ignored when `abs(residual) <= null half-width`.
- A `0.00` setting that explicitly disables filtering.
- Per-range reporting of raw residual, effective residual, decision, and
  resulting EEPROM bytes.
- Exact comparison of the complete post-quantization 80-byte candidate with
  the current EEPROM.
- An unconditional no-write result when the two images are byte-identical,
  including when the advanced checkbox was selected.

The implementation was exercised on physical hardware in development build
`aa1d3ce`. The Intel GitHub Actions build passed, and the user successfully
completed normal calibration followed by a `0.30` read-only dry run. All 16
residuals were suppressed, the candidate matched the EEPROM exactly, and no USB
write was issued.

The one-time eight-run data-collection interface served its purpose and has
been removed from the final branch. Its external CSV reports and checksums are
retained as validation evidence. **Manual Command** remains available.
Upstream verbose and optional timestamp diagnostics remain because they are
general troubleshooting facilities. EEPROM safety reports, state markers,
readbacks, and checksum manifests remain because they are transaction-safety
records rather than temporary diagnostics.

## In-Progress Calibration Help Integration

Branch `codex/calibration-help` adds an offline
`docs/OpenHantek6022_Calibration_and_EEPROM_Safety.html` guide. Its first
section is a **How to Use — Quick Reference** that clearly separates:

- the routine 16-result offset-calibration workflow, which ends after the
  verified INI is saved; and
- the separate, read-only-first EEPROM decision path, including the `0.30`
  null default and the no-material-change stopping rule.

The branch also:

- labels the inherited PDF as **User Manual (Original Project)**;
- adds **Calibration & EEPROM Safety Guide** and
  **About This Intel macOS Modification** to the Help menu;
- adds a Help button to the normal offset-calibration prompt and EEPROM safety
  dialog without changing or accepting their settings;
- updates the About dialog while retaining original project, maintainer,
  copyright, and firmware attribution;
- packages the guide under the macOS app bundle's
  `Contents/Resources/documents` directory; and
- falls back to the corresponding fork README section if the local guide
  cannot be opened.

This help-only branch must pass the Intel GitHub Actions build and receive
user interface review before it is merged into `main`.

## Current Device Calibration State

- Model: `DSO-6022BL`
- Serial: `8164E42F1CC1`
- Current verified 80-byte calibration-region SHA-256:
  `d2053e26578a3fd2aebc1221d79ec4e0ba6143943bf8c3c28cb64f5b2d81b22e`

The reviewed low-speed calibration was written to the physical EEPROM on
2026-07-17, verified by a complete readback, and confirmed again after a USB
power cycle.

A fresh normal offset calibration on 2026-07-18 saved these low-speed INI
residuals:

| Range | CH1 | CH2 |
| --- | ---: | ---: |
| 20 mV/div | -0.12 | +0.02 |
| 50 mV/div | -0.09 | +0.02 |
| 100 mV/div | -0.07 | +0.01 |
| 200 mV/div | -0.03 | +0.03 |
| 500 mV/div | -0.13 | -0.08 |
| 1000 mV/div | -0.04 | +0.02 |
| 2000 mV/div | -0.16 | 0.00 |
| 5000 mV/div | -0.14 | +0.03 |

All 16 values were inside the selected `0.30` null window. The maximum absolute
residual was `0.16`, mean absolute residual was `0.061875`, and median absolute
residual was `0.035` ADC count. The dry-run candidate was therefore
byte-identical to the EEPROM and the physical EEPROM remained unchanged.

The active INI contains:

- `[offset]`: the small low-speed residuals listed above.
- `[offset_high]`: the preserved high-speed residual corrections.
- `[eeprom] replace_eeprom=false`: load the hardware EEPROM and enhance it with
  the appropriate INI residuals.

At sample rates below 30 MS/s, the application uses the low-speed EEPROM values
plus `[offset]`. At 30 MS/s and above, it uses the protected high-speed EEPROM
values plus `[offset_high]`.

## Runtime Calibration Files

These files are outside the source repository and are not moved when the
repository folder is relocated.

Active INI:

```text
/Users/alanbarron/Library/Application Support/OpenHantek/OpenHantek6022/Calibration/DSO-6022BL_8164E42F1CC1_calibration.ini
```

Previous INI backup:

```text
/Users/alanbarron/Library/Application Support/OpenHantek/OpenHantek6022/Calibration/DSO-6022BL_8164E42F1CC1_calibration.ini.bak
```

EEPROM backup root:

```text
/Users/alanbarron/Library/Application Support/OpenHantek/OpenHantek6022/Calibration/EEPROM Backups/DSO-6022BL_8164E42F1CC1_calibration
```

Retained repeatability evidence:

```text
/Users/alanbarron/Library/Application Support/OpenHantek/OpenHantek6022/Calibration/Offset Repeatability Studies/DSO-6022BL_8164E42F1CC1_calibration/20260718T143624703Z
```

Important timestamped bundles:

- `20260717T224010835Z` — guarded write, exact original backup, candidate,
  verified device readback, INI snapshots, state, result, and checksums.
- `20260717T224919851Z` — read-only verification after the USB power cycle.
- `20260718T143624703Z` — complete eight-run repeatability dataset: 128 of 128
  results, unchanged active INI and EEPROM, raw frame log, run means,
  per-range statistics, report, and checksums.
- `20260718T154400738Z` — `0.30` null-window dry run after fresh normal
  calibration; 16 of 16 residuals ignored, exact candidate equality, and
  `EEPROM-no-material-change-report.txt`.
- `20260718T165220276Z` — final production-build smoke test with the same
  `0.30` no-material-change result; all manifest entries verified.

The repeatability analysis produced a provisional global half-width of `0.29`
ADC count. The production default was rounded upward to `0.30`. The study
observed one or more ranges dominating the suggested width by more than twice
the median, so a future threshold change should be supported by new data across
another day, temperature condition, or USB power cycle.

The EEPROM itself is physical memory inside the oscilloscope. The `.bin` files
are exact backup or readback copies of its calibration region.

## How Future Calibration Works

1. Short both inputs, enable both channels, select 10–100 kS/s, and run
   **Calibrate Offset**.
2. Wait for all 16 combinations to complete. The verified residual calibration
   is saved to `[offset]` in the INI and becomes immediately active.
3. The EEPROM remains unchanged. Normally, no further action is required.
4. If incorporating low-speed residuals into EEPROM is justified, open
   **EEPROM Calibration Safety**, retain the hardware-validated `0.30` null
   half-width unless new data supports another value, and leave the advanced
   checkbox unchecked.
5. Review the fresh read-only report, candidate bytes, and checksums.
6. Stop if the result is `NO MATERIAL CHANGE; EEPROM NOT WRITTEN`.
7. Only for a reviewed, materially different candidate, repeat the action,
   explicitly select the advanced option, and accept the separate physical
   confirmation.
8. Keep the scope, USB link, and Mac powered until the complete readback and
   transaction result are displayed. Preserve the timestamped bundle.

The advanced checkbox cannot override the byte-equality guard. After filtering
and EEPROM quantization, an identical 80-byte candidate exits before any
transaction state or USB write command.

## Safety and Working Preferences

- Never write EEPROM automatically.
- Always create and review a fresh read-only safety bundle first.
- Preserve the exact EEPROM and INI backups.
- Never merge hardware-related work until the user tests it successfully.
- Keep temporary diagnostic code explicitly tracked and remove it after it is
  no longer needed.
- Retain **Manual Command** and general upstream diagnostics unless the user
  explicitly requests their removal.
- Retain EEPROM safety reports, state files, readbacks, and checksums.
- Provide direct links to generated logs, reports, and safety folders.
- Prefer small, focused changes without unnecessary code or logging.
- Keep unrelated user files and worktree changes untouched.

Git identity:

```text
AB <174647079+TempAB@users.noreply.github.com>
```

Commits should include:

```text
Signed-off-by: AB <174647079+TempAB@users.noreply.github.com>
```

## Local-Only Project Notes

The repository-root `.codex-notes` directory is ignored by Git. It currently
contains:

```text
.codex-notes/OpenHantek6022 Folder info.rtf
```

Temporary editor lock files created inside that directory are ignored as well.
Close the application editing the RTF before moving the repository. Moving the
complete repository folder will carry `.codex-notes`; cloning from GitHub will
not.

## Status and Resume Procedure

The clean final branch `codex/eeprom-calibration-final` was based directly on
the previous `origin/main` at `6858ac6`. It intentionally excludes the
temporary study interface and collection implementation while preserving only
the final production null-window and identical-candidate safeguards. The
hardware-tested final implementation, public attribution notice, and corrected
fork build instructions have been fast-forwarded into `main`.

After relocating or freshly cloning the repository, start a new Codex local
project at the new folder and ask it to read this file and the calibration
sections of `README.md` before changing anything.

Run:

```sh
git status --short --branch
git remote -v
git fetch --all --prune
git rev-parse HEAD
git log --oneline --decorate -12
```

Confirm that:

- `origin` points to the user's repository.
- `openhantek` points to the original project.
- `main` matches `origin/main` before starting new work.
- The intended calibration branch or merged final commit is present.
- No repeatability-study action or collection code has been reintroduced.
- Any untracked local documents are intentionally preserved or excluded.
