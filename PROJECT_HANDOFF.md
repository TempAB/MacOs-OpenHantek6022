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
- Main implementation baseline: `521a1a926bf9023ede0b7a680be6d40e3aa032fd`

The handoff-document commit is expected to be a documentation-only descendant
of that implementation baseline.

## Build Process

- GitHub Actions workflow: `.github/workflows/build.yml`
- Target artifact: self-contained Intel `x86_64` macOS DMG
- Do not install a local compiler toolchain unless the user explicitly requests
  it.
- Do not merge untested hardware changes.

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
- Waits for stable measurements instead of stopping on one transient frame.
- Requires all 16 channel/range combinations.
- Cancels an incomplete calibration without changing the INI.
- Saves and verifies a completed calibration immediately.
- Creates an INI backup before replacement.

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

## Current Device Calibration State

- Model: `DSO-6022BL`
- Serial: `8164E42F1CC1`
- Current verified 80-byte calibration-region SHA-256:
  `d2053e26578a3fd2aebc1221d79ec4e0ba6143943bf8c3c28cb64f5b2d81b22e`

The reviewed low-speed calibration was written to the physical EEPROM,
verified by a complete readback, and confirmed again after a USB power cycle.

The active INI contains:

- `[offset]`: zero for all 16 low-speed residuals because those values are now
  in the EEPROM.
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

Important timestamped bundles:

- `20260717T224010835Z` — guarded write, exact original backup, candidate,
  verified device readback, INI snapshots, state, result, and checksums.
- `20260717T224919851Z` — read-only verification after the USB power cycle.

The EEPROM itself is physical memory inside the oscilloscope. The `.bin` files
are exact backup or readback copies of its calibration region.

## How Future Calibration Works

1. Run **Calibrate Offset** and complete all 16 combinations.
2. The new residual calibration is saved to the INI and is immediately active.
3. The EEPROM remains unchanged.
4. If the user later wants to incorporate the new low-speed residuals into the
   EEPROM, first run **EEPROM Calibration Safety** with the advanced checkbox
   unchecked.
5. Review the read-only candidate and report.
6. Only after review, repeat the action, explicitly select the advanced option,
   and accept the second confirmation.

Do not perform an EEPROM update when the dry-run candidate is identical to the
device. The current implementation would still issue the four permitted writes
if the advanced operation were explicitly authorized. A useful future safety
improvement is to detect an identical candidate and stop without writing.

## Safety and Working Preferences

- Never write EEPROM automatically.
- Always create and review a fresh read-only safety bundle first.
- Preserve the exact EEPROM and INI backups.
- Never merge hardware-related work until the user tests it successfully.
- Keep temporary diagnostic code explicitly tracked and remove it after it is
  no longer needed.
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

All implemented application changes were hardware-tested, merged, and pushed.
There are no pending code changes.

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

- `main` matches `origin/main`.
- The history contains implementation baseline `521a1a9`.
- `origin` points to the user's repository.
- `openhantek` points to the original project.
- Any untracked local documents are intentionally preserved or excluded.
