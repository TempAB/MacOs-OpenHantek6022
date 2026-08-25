# Project Startup Requirements

Before changing any files:

1. Read `PROJECT_HANDOFF.md`.
2. Read the offset-calibration and EEPROM-calibration sections of `README.md`.
3. Run `git status --short --branch`, `git remote -v`, and
   `git log --oneline --decorate -12`.
4. Report any unexpected branch, remote, or existing file changes before
   proceeding.

# Platform and Release Protections

- The validated Intel application and
  `v3.4.1-rc2-macos-intel.1` release already exist. Do not rebuild, replace,
  convert, or retag them as part of Apple Silicon work.
- The validated Apple Silicon pre-release is
  `v3.4.1-rc2-macos-arm64.1`, targeting remote commit
  `3dc09587083174cde8b808cf2382454d01ec8123`. Do not move, delete, recreate, or
  overwrite that tag or release without explicit user authorization.
- Local ARM commit `fdf981b` and released remote commit `3dc0958` have the same
  tree but different commit identities. Fetch and compare before pushing; do
  not force-push the local history over the released remote history.
- Keep Intel and Apple Silicon DMGs separate. Build ARM changes only through
  `.github/workflows/build-arm64.yml`; do not install a local compiler toolchain
  unless the user explicitly asks.
- A release uses the inner architecture-specific DMG and `SHA256SUMS.txt`, not
  the enclosing GitHub Actions artifact ZIP. GitHub generates source ZIP and
  TAR links from the release tag.
- Before reporting that GitHub publication is blocked, inspect the currently
  available GitHub connector, browser, and CLI capabilities. The ARM `.1`
  release required no installation: its tag, release, and assets were created
  through an already authenticated GitHub browser session, then verified with
  read-only GitHub API calls.

# M4 Hardware and Calibration Guardrails

- For a DSO-6022BL, press the **H/P** button before connecting. USB ID
  `0925:3881` is logic/programmer mode and can make the app open in demo mode;
  the validated scope-mode identity is `04b5:602a`.
- If traces stop and logs repeat libusb `Input/Output Error`, quit the app and
  remove USB power from the scope for about one minute before changing code or
  calibration.
- The M4 already has the validated device-specific calibration for
  DSO-6022BL serial `8164E42F1CC1`. Do not recreate calibration merely to test
  ARM support, and never perform an EEPROM write without the established
  read-only review and explicit user authorization.
- Do not commit, bundle, or publish device-specific calibration files.
- Preserve unrelated user files, including an untracked
  `docs/HT6022BL_Manual.pdf`, unless the user explicitly directs otherwise.
