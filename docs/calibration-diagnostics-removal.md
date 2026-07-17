# Calibration diagnostics removal manifest

`CAL_DIAG_TEMP` marks code that exists only on `codex/calibration-diagnostics`.
This branch is a dry run and must not be merged into `main`.

Temporary files:

- `.gitignore` entry for `diagnostic-logs/`
- `docs/calibration-diagnostics-removal.md`
- `openhantek/src/hantekdso/calibrationdiagnostics.h`
- `openhantek/src/hantekdso/calibrationdiagnostics.cpp`

Generated local output (ignored by Git):

- `diagnostic-logs/offset-calibration-diagnostic.log`

On the diagnostic Mac, the direct path is:

`/Users/alanbarron/Hantek6022BL/OpenHantek6022/diagnostic-logs/offset-calibration-diagnostic.log`

Temporary hooks:

- `openhantek/src/hantekdso/hantekdsocontrol.h`
  - Forward declaration and `calibrationDiagnostics` member.
  - `calibrationDiagnosticDryRun` persistence guard.
- `openhantek/src/hantekdso/hantekdsocontrol.cpp`
  - Diagnostic include.
  - Start/session baseline logging in `calibrateOffset()`.
  - Per-frame decision logging in `convertRawDataToSamples()`.
  - Dry-run guards in `calibrateOffset()` and `updateCalibrationValues()`.
- `openhantek/src/mainwindow.cpp`
  - Diagnostic dry-run wording in the calibration confirmation.

Removal verification required before any production calibration fix is merged:

```text
rg -n "CAL_DIAG_TEMP|CalibrationDiagnostics|calibrationDiagnostic" .
```

The command must return no results on the production fix branch.
