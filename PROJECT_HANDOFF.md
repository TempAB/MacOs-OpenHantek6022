# OpenHantek6022 macOS Project Handoff

## Purpose

This repository is the user's macOS-focused fork of OpenHantek6022. The
existing Intel application is complete, hardware-tested, and must not be
recreated as part of Apple Silicon work. A separate native `arm64` application
for a Mac mini M4 has now been built, physically validated, and published as a
platform-specific pre-release. The user does not want to install or maintain a
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
- Calibration Help implementation: `3d2a2e0` and `35caf79`
- Calibration Help branch: `codex/calibration-help`
- Apple Silicon branch: `codex/apple-silicon-arm64`
- Released Apple Silicon commit: `3dc09587083174cde8b808cf2382454d01ec8123`
- Apple Silicon release tag: `v3.4.1-rc2-macos-arm64.1`
- Apple Silicon pre-release:
  <https://github.com/TempAB/MacOs-OpenHantek6022/releases/tag/v3.4.1-rc2-macos-arm64.1>

The final production branch was created directly from `origin/main`. It contains
the hardware-validated null-window and identical-candidate safeguards without
the temporary repeatability-study interface or collection code. After the
public-facing README attribution was added, this branch was fast-forwarded into
`main`.

The local Apple Silicon head is `fdf981b`, while the immutable release tag
points to remote commit `3dc0958`. Those two pre-documentation commits have the
identical source tree `65e0a350630e341e052b95955c3443b4d4a9252b`. The remote
ARM branch may advance beyond `3dc0958` with documentation or later approved
work, but the release tag must remain fixed. Do not force-push the local commit
over the released remote history.

## Build Process

- Apple Silicon workflow: `.github/workflows/build-arm64.yml`
- Published ARM artifact: self-contained native `arm64` macOS DMG
- Existing Intel workflow: `.github/workflows/build.yml`, retained for manual
  dispatch only
- Keep the Intel and Apple Silicon DMGs separate. ARM development must not
  rebuild, replace, or convert the existing Intel application.
- Do not install a local compiler toolchain unless the user explicitly requests
  it.
- Do not merge hardware-related changes until the user has tested the build.
- For GitHub Releases, upload the DMG and its `SHA256SUMS.txt` from inside the
  Actions artifact. Do not publish the enclosing, temporary Actions ZIP as the
  application download.

## Completed and Hardware-Tested Work

### Intel macOS packaging

The GitHub Actions workflow builds a self-contained Intel macOS application and
DMG. Relevant commits include:

- `40d59e8` — build self-contained Intel macOS DMG
- `e0e3c8e` — avoid rebundling Qt frameworks
- `495f801` — audit Mach-O load commands accurately

### Apple Silicon ARM64 expansion

Branch `codex/apple-silicon-arm64` adds a separate native ARM64 packaging path
for the Mac mini M4. The ARM workflow:

- runs on GitHub's `macos-15` Apple Silicon runner;
- explicitly supplies the Homebrew prefixes for Qt 6, libusb, and FFTW;
- requires the main executable and every bundled Mach-O file to contain
  `arm64`;
- retains the non-system dependency audit and ad-hoc signature verification;
- verifies the completed DMG and publishes an `OpenHantek-...-macos-arm64`
  artifact with its own SHA-256 manifest; and
- does not run or replace the existing Intel packaging path.

The generic ARM throttles intended for lower-power systems such as Raspberry
Pi are excluded only on macOS, so Apple Silicon uses the established desktop
acquisition and display intervals. Intel behavior is unchanged. No OpenGL,
offset-calibration, or EEPROM-calibration algorithm is changed in this work.

GitHub Actions Run 6 completed successfully for remote commit `3dc0958`:

<https://github.com/TempAB/MacOs-OpenHantek6022/actions/runs/32890006498>

The build identifier is `3.4.1-rc2-27-g3dc0958-macos-arm64`. The workflow
verified the native runner, main executable and all bundled Mach-O files as
`arm64`, rejected unbundled non-system dependencies, verified the ad-hoc code
signature, and validated the completed DMG. The uploaded Actions artifact ZIP
has SHA-256
`a5884476b94618329e4151fe8f70214d94948e62a94b7067b78325110e021bfd`.

### Apple Silicon M4 hardware validation

The released build was tested on a Mac mini M4 with the existing Hantek
DSO-6022BL, serial `8164E42F1CC1`.

Initial discovery failed and the application opened in demo mode because the
6022BL was connected in its logic/programmer USB mode (`0925:3881`). The
corrective action is to disconnect the device, press the **H/P** button, and
reconnect it in oscilloscope mode. The scope then enumerated as `04b5:602a` and
appeared in the application immediately. Do not treat `0925:3881` as an ARM
application or USB-C adapter regression.

The physical validation confirmed:

- the copied device-specific calibration was discovered and applied;
- both channels were centred near zero;
- zero and 2 V reference signals retained the correct amplitude and zero
  positions;
- the normal maximum 12 MS/s acquisition was stable;
- channel controls remained responsive;
- normal rising and falling trigger behavior, including threshold response,
  worked correctly;
- clean quit/reopen and USB reconnect both rediscovered the scope; and
- the About information reported Apple Silicon/`arm64`, the exact build
  identifier, DSO-6022BL, serial number, firmware, OpenGL, and GLSL details.

One acquisition session stopped after the probes were connected to the 2 V
reference. Runtime logging showed repeated libusb `Input/Output Error`
messages. Quitting the application and removing USB power from the scope for
about one minute restored normal operation; the failure did not recur during
the subsequent signal, control, trigger, restart, or reconnect checks. If the
same symptom appears, use a full USB power cycle before investigating the ARM
application or calibration data.

### Apple Silicon pre-release publication

The validated ARM build was published separately from the Intel build on
2026-08-25:

- Release tag: `v3.4.1-rc2-macos-arm64.1`
- Release target: `3dc09587083174cde8b808cf2382454d01ec8123`
- Release title: `OpenHantek6022 3.4.1-rc2 — Apple Silicon macOS Fork, Release 1`
- DMG: `OpenHantek-3.4.1-rc2-27-g3dc0958-macos-arm64.dmg`
- DMG SHA-256:
  `897ed1cff48b5c712bd50dccc10c3b385f94e5ecb5ee9ada9559cc89c785b0c6`
- Checksum asset: `SHA256SUMS.txt`

The release is marked **Pre-release** and is not the latest production release.
The DMG checksum manifest and a fresh local `hdiutil verify` both passed before
publication. GitHub supplies the source ZIP and TAR archives automatically, so
the release shows four assets: DMG, checksum manifest, source ZIP, and source
TAR. The web release form created a lightweight tag that points directly to the
tested commit; this differs from the annotated Intel tag but does not change
the release downloads, generated source archives, or provenance.

The ARM application remains ad-hoc signed and is not Developer ID signed or
notarized. Preserve the Gatekeeper first-launch guidance in the release notes.
Device-specific calibration files must never be bundled into a DMG or attached
to a public release.

At the time of this publication, the connected GitHub integration supported
release and tag inspection but did not expose tag creation, release creation,
or release-asset upload. The local GitHub CLI was not installed and local HTTPS
Git credentials were not available. No installation was necessary: the release
was created through the already authenticated in-app GitHub page, and the
published tag, target, prerelease state, asset names, sizes, and SHA-256 digests
were then verified through the GitHub API. Recheck available capabilities for a
future release instead of assuming an installation or new login is required.

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

## Completed and User-Tested Calibration Help Integration

Branch `codex/calibration-help` added an offline
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

Intel workflow Run 28 passed for commit `3d2a2e0`. The user confirmed that the
normal-calibration and EEPROM-dialog Help buttons open the correct guide
sections without starting calibration or an EEPROM operation. Initial macOS
review found that Qt automatically moved all three `About...` actions out of
Help because they inherited `TextHeuristicRole`. The follow-up explicitly sets
those three actions to `QAction::NoRole` so they remain in the requested Help
menu.

Intel workflow Run 29 passed for follow-up commit `35caf79`. The user installed
build `OpenHantek-3.4.1-rc2-18-g35caf79-macos-x86_64` and confirmed that all
three About actions are present and work correctly. The complete Help
integration is therefore built and user-tested, and the user authorized its
final merge and push to `main`.

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

On 2026-08-25 the calibration files were copied to the standard location on
the Mac mini M4. The active INI SHA-256 is
`88caf72ec6227cc513810e127dff5063d9f3ccb8dd51902effe03d8918b0234b`, the
EEPROM reference SHA-256 remains
`d2053e26578a3fd2aebc1221d79ec4e0ba6143943bf8c3c28cb64f5b2d81b22e`, and
all four retained SHA-256 manifests verified successfully. The copied INI
contains `[offset]`, `[offset_high]`, and `[eeprom] replace_eeprom=false`.
The intended M4 validation sequence was therefore to verify discovery and
loading of this existing calibration, not to create a replacement calibration
or perform an EEPROM write.

That M4 validation is now complete. The copied calibration produced correct
zero and 2 V measurements on both channels at the normal 12 MS/s maximum. No
new offset calibration or EEPROM operation is required. The preserved
`[offset_high]` data remains dormant in the validated sub-30 MS/s operating
range and must not be deleted merely because it was not exercised.

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
- Keep Intel and Apple Silicon DMGs separate; do not rebuild the validated
  Intel application as part of ARM development.
- Do not move, delete, or recreate either published platform release tag unless
  the user explicitly requests a release replacement.
- Do not include device-specific calibration files in source control, a DMG,
  an Actions artifact intended for publication, or a GitHub Release.
- Treat a DSO-6022BL enumerating as `0925:3881` as the wrong H/P mode. Press
  **H/P** before reconnecting and expect `04b5:602a` in scope mode.
- For a stalled acquisition with repeated libusb `Input/Output Error`, quit the
  application and fully remove USB power from the scope before changing code
  or calibration.
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
fork build instructions have been fast-forwarded into `main`. The completed,
user-tested calibration Help integration from `codex/calibration-help` has
also been fast-forwarded into `main`.

Apple Silicon work on `codex/apple-silicon-arm64` passed GitHub Actions,
completed physical Mac mini M4 validation with the copied calibration files,
and was published as the separate pre-release
`v3.4.1-rc2-macos-arm64.1`. On 2026-08-25, after explicit user approval, the
validated remote ARM lineage and its final handoff documentation were merged
into `main`. The existing Intel application was not rebuilt, replaced, or
converted by that merge.

Before resuming ARM work, fetch the remote branches and remember that local
`fdf981b` and released remote `3dc0958` have the same pre-documentation tree but
different commit identities. Treat the remote lineage as authoritative and do
not force-update the release tag or remote branch. If a later application
change is needed, build it through GitHub Actions, repeat proportionate M4
hardware validation, and use a new ARM release number rather than replacing
the published `.1` release.

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
- `main` contains the merged Apple Silicon expansion and the released ARM
  commit remains in its ancestry.
- The released ARM tag still resolves to `3dc0958` and the ARM pre-release and
  its two uploaded assets remain available.
- No repeatability-study action or collection code has been reintroduced.
- Any untracked local documents are intentionally preserved or excluded.
