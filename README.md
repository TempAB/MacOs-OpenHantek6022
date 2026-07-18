# OpenHantek6022 — Intel macOS-focused modification

[![Intel macOS DMG](https://github.com/TempAB/MacOs-OpenHantek6022/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/TempAB/MacOs-OpenHantek6022/actions/workflows/build.yml)
[![Original project](https://img.shields.io/badge/original-OpenHantek%2FOpenHantek6022-blue)](https://github.com/OpenHantek/OpenHantek6022)

> [!IMPORTANT]
> This repository is an independently maintained, Intel macOS-focused modification of
> [OpenHantek6022](https://github.com/OpenHantek/OpenHantek6022), originally developed by Martin
> Homuth-Rosemann and the OpenHantek contributors. It is not an official upstream release or a replacement for the
> original cross-platform project. If you are seeking the original Linux, Windows, macOS, or other supported builds,
> documentation, releases, or project support, please use
> **[OpenHantek/OpenHantek6022](https://github.com/OpenHantek/OpenHantek6022)**.

This modification is maintained for a specific Intel `x86_64` Mac and has been hardware-tested with a Hantek
DSO-6022BL. It adds a self-contained Intel macOS DMG workflow, consistent selector behavior, safer persistent offset
calibration, native macOS calibration storage, guarded EEPROM-calibration tools, and a bundled
[Calibration & EEPROM Safety Guide](docs/OpenHantek6022_Calibration_and_EEPROM_Safety.html). The fork does not run
upstream's Linux, Windows, Apple Silicon, or other device test matrix.

OpenHantek6022 is free software for **Hantek DSO6022** USB digital signal oscilloscopes. The underlying project
supports Hantek 6022BE/BL and compatible scopes such as Voltcraft, Darkwire, Protek, and Acetech devices. Most general
project information below is inherited from upstream; fork-specific behavior is identified explicitly.

**This project gives no support for its [currently unmaintained](https://github.com/OpenHantek/openhantek/issues/277) predecessor [openhantek](https://github.com/OpenHantek/openhantek).**

<p><img alt="Image of main window on linux" width="100%" src="docs/images/screenshot_mainwindow.png"></p>

#### Content
* [About This Modification](#about-this-modification)
* [About OpenHantek6022](#about-openhantek6022)
* [Features](#features)
* [AC Coupling](#ac-coupling)
* [Continuous Integration](#continuous-integration)
* [Building OpenHantek6022 from source](#building-openhantek6022-from-source)
* [Install Prebuilt Binary Packages](#install-prebuilt-binary-packages)
* [Run OpenHantek6022](#run-openhantek6022)
  + [Offset Calibration](#offset-calibration)
  + [In-app Calibration Help](#in-app-calibration-help)
  + [OpenGL Support](#opengl-support)
  + [USB Access](#usb-access)
* [Important!](#important)
* [Specifications, Features, Limitations and Developer Documentation](#specifications-features-limitations-and-developer-documentation)
* [Contribute](#contribute)
* [Other DSO Open Source Software](#other-dso-open-source-software)
* [Other Related Software](#other-related-software)
* [History](#history)
* [Donate](#please-help-the-victims-of-the-war)
<!-- * [Donate for FSF](#donate-for-fsf) -->

## About This Modification

This repository preserves the original project's license and history while maintaining a focused set of changes for
Intel macOS use. The principal differences are:

* A GitHub Actions workflow that builds, audits, ad-hoc signs, and packages a self-contained Intel `x86_64` DMG.
* Standardized hover-wheel, keyboard, and native popup behavior for selectors.
* Offset calibration that requires all 16 channel/range combinations, rejects unstable measurements, verifies the
  saved INI, and never writes EEPROM automatically.
* Native macOS calibration storage with verified migration from the legacy configuration location.
* Read-only-first EEPROM preparation, exact backups, SHA-256 reports, guarded low-speed writes, complete readback,
  automatic rollback, an adjustable null window, and an unconditional no-write result for byte-identical candidates.
* A bundled offline calibration and EEPROM safety guide with a first-page quick reference, Help buttons at both
  calibration decision points, and clear attribution to the original project.

These changes are documented in the [Offset Calibration](#offset-calibration) section. Users who need the original
multi-platform project should use [OpenHantek/OpenHantek6022](https://github.com/OpenHantek/OpenHantek6022).

## About OpenHantek6022
* OH6022 works with digital USB 2 channel scopes that are based on the Cypress EzUSB processor,
these devices are typically announced as scopes with a max. sample rate of 48 MS/s
(even if this theoretical rate works not reliable on most systems due to USB protocol overhead).  
Supported devices:
  - **Hantek 6022BE** and **6022BL** as well as compatible scopes (e.g. **Voltcraft DSO-2020**).
  - **SainSmart DDS120** (thx [msiegert](https://github.com/msiegert)) - this device has a different
  analog front end and uses the
  [slightly improved sigrok firmware](https://github.com/Ho-Ro/sigrok-firmware-fx2lafw),
  which has [some limitations](https://sigrok.org/wiki/SainSmart_DDS120/Info#Open-source_firmware_details)
  compared to the Hantek scopes (see [#69](https://github.com/OpenHantek/OpenHantek6022/issues/69#issuecomment-607341694)).
* Demo mode is provided by the `-d` or `--demoMode` command line option.
* The original project's fully supported operating system is Linux, developed under Debian stable (currently
  *trixie*) for amd64, with additional Raspberry Pi, FreeBSD, Windows, and macOS build coverage described upstream.
* This modification is hardware-tested on Intel `x86_64` macOS only. It does not provide or imply Linux, Windows,
  Apple Silicon, FreeBSD, Raspberry Pi, or general upstream support.
* Uses [free open source firmware](https://github.com/Ho-Ro/Hantek6022API), no longer dependent on nonfree Hantek firmware.
* Extensive [User Manual](docs/OpenHantek6022_User_Manual.pdf) with technical specs and schematics.

## Features
* Voltage and Spectrum view for all device supported channels.
* CH1 and CH2 name becomes red when input is clipped (bottom left).
* Settable probe attenuation factor 1..1000 to accommodate a variety of different probes.
* Measure and display Vpp, DC (average), AC, RMS and dB (of RMS) values as well as frequency of active channels.
  Display as *dBV* (0 dBV = 1 V rms), *dBu* (0 dBu = 1 mW @ 600 Ω) or *dBm* (0 dBu = 1 mW @ 50 Ω) can be selected in Oscilloscope/Settings/Analysis.
* Display the power dissipation for a load resistance of 1..1000 Ω (optional, can be set in Oscilloscope/Settings/Analysis).
* Display the THD of the signal (optional, can be enabled in Oscilloscope/Settings/Analysis).
* Show the musical note values and deviation in cent (*twelve equal*, A = 440 Hz) for audio frequencies
  (optional, can be enabled in Oscilloscope/Settings/Analysis). Useful to tune e.g. your electrical guitar.
* Math channel arithmetical modes for CH1 and CH2:  
  CH1+CH2, CH1-CH2, CH2-CH1, CH1*CH2
* Math channel analog modes for CH1 or CH2:  
  Square, abs, sign, AC and DC part, linear phase low pass filtering
* Math channel logical modes for CH1 and CH2:  
  And, or, xor, equality, comparison
* Math channel logical modes for CH1 or CH2:  
  Compare with trigger level
* Time base 10 ns/div .. 10 s/div.
* Sample rates 100, 200, 500 S/s, 1, 2, 5, 10, 20, 50, 100, 200, 500 kS/s, 1, 2, 5, 10, 12, 15, 24, 30 MS/s (24 & 30 MS/s in CH1-only mode, 48 MS/s not supported due to unstable USB data streaming).
* Hardware input gain automatically selected based on vertical sensitivity: 1x (up to ±5 V for 1, 2 or 5 V/div), 2x (up to ±2.5 V for 500 mV/div), 5x (up to ±1 V for 200 mV/div) and 10x (up to ±500 mV for 20 or 50 mV/div).
* Downsampling (up to 200x) increases resolution and SNR.
* Calibration output square wave signal frequency can be selected between 32 Hz .. 100 kHz in small steps (*poor person's* signal generator).
A [little HW modification](docs/HANTEK6022_Frequency_Generator_Modification.pdf) provides a jitter free HW-driven calibration output signal instead of the interrupt driven SW-output.
* Trigger source: *CH1*, *CH2*, *MATH*
* Trigger modes: *Normal*, *Auto* and *Single* with green/red status display (top left).
* Untriggered *Roll* mode can be selected for slow time bases of 200 ms/div .. 10 s/div.
* Trigger filter *HF* (trigger also on glitches), *Normal* and *LF* (for noisy signals).
* Display interpolation modes *Off*, *Linear*, *Step* and *Sinc*.
* Calibration values loaded from eeprom or a model configuration file.
* Online offset calibration creates a configuration file for persistent data storage.
* [Calibration program](https://github.com/Ho-Ro/Hantek6022API/blob/main/README.md#create-calibration-values-for-openhantek) to create these values automatically.
* Digital phosphor effect to notice even short spikes; simple eye-diagram display with alternating trigger slope.
* Histogram function for voltage channels on right screen margin.
* A [zoom view](docs/images/screenshot_mainwindow_with_zoom.png) with a freely selectable range.
* Cursor measurement function for voltage, time, amplitude and frequency.
* Export of the graphs to JPG, PNG or PDF file or to the printer; data export as CSV or JSON. 
* Freely configurable colors.
* Selectable font size and font weight.
* Automatic adaption of iconset for light and [dark themes](docs/images/screenshot_mainwindow_dark.png).
* The dock views on the main window can be [customized](https://github.com/OpenHantek/OpenHantek6022/issues/161#issuecomment-799597664) by dragging them around and stacking them.
  This allows a minimum window size of 800*300 for old laptops or workstation computers.
* All settings will be saved to a configuration file and loaded again.
* French, German, Italien, Russian and Spanish localisation complete; Chinese, Polish and Swedish is updated regularily; Portuguese translation ongoing - [volunteers welcome](openhantek/translations/Translation_HowTo.md)!

## AC Coupling
A [little HW modification](docs/HANTEK6022_AC_Modification.pdf) adds AC coupling. OpenHantek6022 supports this feature since v2.17-rc5 / FW0204.

## Continuous Integration

Pushes to this fork's `main` and `codex/**` branches run the
[Intel macOS DMG workflow](https://github.com/TempAB/MacOs-OpenHantek6022/actions/workflows/build.yml). It builds on
GitHub's `macos-15-intel` runner, bundles non-system runtime dependencies, verifies the application is `x86_64`,
audits Mach-O load commands for portability, verifies the ad-hoc code signature, and uploads a DMG plus SHA-256
manifest. Workflow artifacts are retained for 30 days.

This fork does not run Linux, Windows, Apple Silicon, or general release packaging jobs. Refer to the
[original project's workflows](https://github.com/OpenHantek/OpenHantek6022/actions) for upstream multi-platform
builds.

## Building OpenHantek6022 from source

To build this Intel macOS-focused modification, clone this repository:

```sh
git clone https://github.com/TempAB/MacOs-OpenHantek6022.git
```

To build the original cross-platform project instead, clone upstream:

```sh
git clone https://github.com/OpenHantek/OpenHantek6022.git
```

Building locally requires:

* [CMake 3.12+](https://cmake.org/download/)
* [Qt 6.2+](https://www1.qt.io/download-open-source/)
* [FFTW 3+](http://www.fftw.org/) (prebuild files will be downloaded on windows)
* [libusb-1.0](https://libusb.info/), version >= 1.0.16 (prebuild files will be used on windows)
* A compiler that supports C++11 - tested with gcc, clang and msvc

We have build instructions available for [Linux](docs/build.md#linux), [Raspberry Pi](docs/build.md#raspberrypi), [FreeBSD](docs/build.md#freebsd), [Apple macOS](docs/build.md#macos) and [Microsoft Windows](docs/build.md#windows).  
For RPi4 see also [issue #28](https://github.com/OpenHantek/OpenHantek6022/issues/28).

To make building for Linux even easier, I provide two shell scripts:

* [`LinuxSetup_AsRoot`](LinuxSetup_AsRoot), which installs all build requirements. You only need to call this script once (as root) if you have cloned the project.
* [`LinuxBuild`](LinuxBuild) configures the build, builds the binary and finally creates the packages (deb, rpm and tgz) that can be installed as described in the next paragraph.

If you make small changes to the local source code, it is sufficient to call `make -j4` or `fakeroot make -j4 package` in the `build` directory.

## Install Prebuilt Binary Packages

Intel macOS DMGs for this modification are available from successful runs of the fork's
[GitHub Actions workflow](https://github.com/TempAB/MacOs-OpenHantek6022/actions/workflows/build.yml). Select a
successful `main` run and download its `OpenHantek-...-macos-x86_64` artifact. These artifacts are ad-hoc signed,
are not Apple-notarized, and expire after 30 days.

For official upstream multi-platform packages and releases, use the original project's resources below.

* [![Downloads of latest release](https://img.shields.io/github/downloads/OpenHantek/OpenHantek6022/latest/total?color=blue)](https://github.com/OpenHantek/OpenHantek6022/releases/latest)
Download Linux (Ubuntu 2404 LTS), macOS (13) and Windows (MSVC2022) packages for your convenience from the [Releases](https://github.com/OpenHantek/OpenHantek6022/releases) page.
* [![Downloads of latest devdrop](https://img.shields.io/github/downloads/OpenHantek/OpenHantek6022/devdrop/total?color=lightblue)](https://github.com/OpenHantek/OpenHantek6022/releases/tag/devdrop)
If you want to follow ongoing development, packages built from a fairly recent commit are available in the rolling
[devdrop release](https://github.com/OpenHantek/OpenHantek6022/releases/tag/devdrop).
Individual features or elements of the GUI may still change.
* These binary packages are built on stable operating system versions and require an up-to-date system.
* As I develop on a *Debian stable* system my preferred (native) package format is `*.deb`.
The program itself and the `*_amd64.deb` package built on my local system is tested for completeness and correctness.
The precompiled packages are only randomly tested - if at all - and the installation of the `*.rpm` packages is untested.
* To install the downloaded `*.deb` package, open a terminal window, go to the package directory and enter the command (as root) `apt install ./openhantek_..._amd64.deb`.
This command will automatically install all dependencies of the program as well.
* For installation of `*.rpm` packages follow similar rules, e.g. `dnf install ./openhantek-...-1.x86_64.rpm`.
* The `*.tar.gz` achives contain the same files as the `*.deb` and `*.rpm` packages for quick testing.  
Do not use for a permanent installation.  
Do not report any issues about the `*.tar.gz`!
* Get macOS packages from [macports](https://www.macports.org/ports.php?by=name&substr=openhantek) - thx [ra1nb0w](https://github.com/ra1nb0w).
* Get [Fedora rpm packages](https://pkgs.org/download/openhantek) - thx [Vascom](https://github.com/Vascom).
* Download [(untested) builds from last commit(s)](https://github.com/OpenHantek/OpenHantek6022/actions/workflows/build_check.yml). Select the preferred `workflow run` and go to `Artifacts`.

## Run OpenHantek6022
On a Linux system start the program via the menu entry *OpenHantek (Digital Storage Oscilloscope)* or from a terminal window as `OpenHantek`.

You can explore the look and feel of OpenHantek6022 without the need for real scope hardware by running it from the command line as: `OpenHantek --demoMode`.

Note: To use the 6022BL in scope mode, make sure the "H/P" button is pressed before plugging in.

### Using Hantek 6022BL LA Function
The [Hantek6022BL](https://sigrok.org/wiki/Hantek_6022BL) can either be used as oscilloscope or as logic analyzer,
but not both at the same time - it is not a mixed-signal-oscilloscope (MSO).
If you want to use the LA part, then [sigrok](https://sigrok.org) is the way to go, it works (besides Linux) also for MacOS and Windows.
There is no point in supporting the LA input from OpenHantek.

### Offset Calibration

The oscilloscope has a relatively large zero-point error. Normal offset calibration measures the residual error and
stores it in a device-specific INI file; it never writes the oscilloscope EEPROM.

#### In-app calibration help

Choose *Help/Calibration & EEPROM Safety Guide* for the bundled offline guide. Its opening **How to Use — Quick
Reference** separates the normal six-step calibration workflow from the advanced EEPROM decision path. The same
guide is available from the **Help** button in both the *Calibrate Offset* preparation prompt and the *EEPROM
Calibration Safety* dialog.

The Help menu labels the inherited PDF as *User Manual (Original Project)* and keeps the original AC and frequency
generator modification documents. *About This Intel macOS Modification* opens the fork-attribution section of the
offline guide. If the bundled guide cannot be found, the application opens the matching section of this README
online.

#### Required measurement procedure

1. Short-circuit both inputs, for example with 50 Ω terminating plugs or shorted probe inputs.
2. Enable both channels.
3. Select a slow timebase of 10..100 ms/div so the sample rate is between 10 and 100 kS/s.
4. Activate *Oscilloscope/Calibrate Offset*, use **Help** if the quick reference is needed, and accept the
   confirmation.
5. Slowly select all eight voltage ranges for CH1 and CH2. Wait for the status bar to report each range complete before
   moving to the next one.
6. Deactivate *Calibrate Offset* only after all 16 channel/range combinations have been reported complete.

The first frame after each range change is discarded. Two valid, stable measurements are required for a range;
clipped, out-of-range, or unstable measurements are rejected and retried. Ending calibration before all 16
combinations are complete cancels the run without changing the active INI.

A completed calibration is written and verified immediately. If an INI already exists, the previous file is
preserved as `*.ini.bak`; a failed save or verification restores it. At sample rates below 30 MS/s, the application
uses the hardware low-speed EEPROM calibration plus the INI `[offset]` residuals. At 30 MS/s and above, it uses the
protected high-speed EEPROM calibration plus `[offset_high]`. A normal low-speed calibration changes `[offset]` only.

On macOS, calibration files are stored in
`~/Library/Application Support/OpenHantek/OpenHantek6022/Calibration`. An existing calibration file under
`~/.config/OpenHantek` is copied and verified automatically the first time the new location is used; the original is
retained as a migration backup. Use *Oscilloscope/Show Calibration Folder* to reveal the active folder in Finder.

#### EEPROM calibration safety

*Oscilloscope/EEPROM Calibration Safety* is an advanced, manual operation for incorporating saved low-speed INI
residuals into hardware calibration. It is not part of normal offset calibration. The safe default is a read-only dry
run: leave *Also back up and update the device EEPROM (advanced)* unchecked. Select **Help** in this dialog to open
the guide directly at the EEPROM safety section without accepting or changing the dialog.

Every dry run or update reads the complete 80-byte calibration region twice and requires exact agreement. It then
creates a timestamped folder under `Calibration/EEPROM Backups` containing the exact EEPROM backup, active INI
snapshot, proposed low-speed candidate, readable report, and SHA-256 manifest. The report lists the raw and effective
residual, null-window decision, calculated centre, and original/candidate bytes for every channel and range.

The dialog exposes a zero-centred **null half-width** in ADC counts. It applies only while building the low-speed
EEPROM candidate; it does not alter the saved INI, normal calibration measurements, or protected high-speed EEPROM
values. The selector resets to `0.30` whenever the dialog opens and is never persisted.

| Selection | Candidate rule |
| --- | --- |
| `0.00` | Disable null filtering; retain every finite residual. |
| `0.01` through `0.50` | Ignore a residual when `abs(residual) <= null half-width`; retain it when it is outside the window. |
| `0.30` | Hardware-validated default, selected from an eight-run, 128-result repeatability dataset. |

The `0.30` default is deliberately rounded above the dataset's `0.29` statistical starting value. Lower values are
more sensitive to measurement noise and may propose unnecessary EEPROM changes. Higher values may hide a real
low-speed correction. Change the default only when new repeatability data under representative hardware,
temperature, and power-cycle conditions supports a different threshold.

After null filtering, the proposed offsets are converted to the EEPROM byte representation and the entire 80-byte
candidate is compared with the current EEPROM. If it is byte-identical, the result is reported as
`NO MATERIAL CHANGE; EEPROM NOT WRITTEN`. This block applies even if the advanced update option was selected.

#### Rules for a physical EEPROM update

- Complete and save a normal offset calibration first.
- Run a fresh read-only dry run with the intended null half-width. Review the report, candidate, and checksums before
  authorizing any write.
- Repeat *EEPROM Calibration Safety*, explicitly select the advanced option, and accept the separate physical-write
  confirmation. The advanced selection and null value reset every time the dialog opens.
- Keep USB, oscilloscope power, and Mac power stable until the verified result is displayed. Do not disconnect or
  suspend the computer during the transaction.
- Do not attempt a write when the dry run reports no material change. Selecting the advanced option cannot override
  the exact byte-equality guard.
- Preserve the timestamped safety folder. It contains the material required to review or recover the transaction.

An authorized update can write only four aligned 8-byte low-speed offset chunks at EEPROM addresses `0x08`, `0x10`,
`0x38`, and `0x40`. A complete 80-byte readback must match the candidate exactly. Any write, readback, INI, or audit
failure triggers restoration of the original chunks followed by another full readback. After a verified update,
low-speed `[offset]` residuals are zeroed to prevent double correction, the prior high-speed residuals remain under
`[offset_high]`, and `[eeprom] replace_eeprom=false` is verified.

### OpenGL Support
OpenHantek6022 uses the *OpenGL* graphics library to display the data. It requires a graphics card that supports
3D rendering and runs on legacy HW/SW that supports at least *OpenGL* 2.1+ or *OpenGL ES* 1.2+.
*OpenGL* is selected by default, but if this does not work (i.e. the black scope window shows an error message
or closes immediately after startup), you can choose the less resource-hungry *OpenGL ES* variant as a fallback
by starting OpenHantek from the command line as follows: `OpenHantek -e` or `OpenHantek --useGLES`.

Especially on Windows, this option may be necessary to use the program.

It has been reported that the MINGW binary build on some Windows systems had problems with the graphical display
and led to a black screen without traces. In these cases, the switch to the MSVC binary build can help.

Similar [issues](https://github.com/OpenHantek/OpenHantek6022/issues/350) with Linux on ChromeOS (Crostini) can be solved by setting the environment variable `LIBGL_ALWAYS_SOFTWARE=1` when using OpenHantek.
This could also be a solution for the above MINGW issue, see e.g. [#360](https://github.com/OpenHantek/OpenHantek6022/issues/360)
and [#388](https://github.com/OpenHantek/OpenHantek6022/issues/388) - not yet confirmed.

The Raspberry Pi build uses OpenGL ES automatically, check also the [graphics driver setup](docs/build.md#raspberrypi).

### USB Access
USB access for the device is required (unless using demo mode):
* Starting with version 3.4.1 the program detects USB disconnections and tries to reconnect instead of terminating with an error.
* **_Linux/Unix_**  
You need to copy the file `utils/udev_rules/60-openhantek.rules` to `/etc/udev/rules.d/` or `/usr/lib/udev/rules.d/` and replug your device.
Note: If OpenHantek is installed from a `*.deb` or `*.rpm` package this file is installed automatically into `/usr/lib/udev/rules.d/`.
* **_Windows_**  
__Caution: The original Hantek driver for Windows doesn't work!__  
__You have to assign the correct WinUSB driver:__

  - The signed `.inf` file `OpenHantek.inf` for all devices - [provided by VictorEEV](https://www.eevblog.com/forum/testgear/hantek-6022be-20mhz-usb-dso/msg4418107/#msg4418107)
  and [updated](https://github.com/OpenHantek/OpenHantek6022/pull/323) by [gitguest0](https://github.com/gitguest0) - 
  is available in the `openhantek_xxx_win_x64.zip` [binary distribution](https://github.com/OpenHantek/OpenHantek6022/releases) in directory `driver`.

  - Right-click on `OpenHantek.inf` and select "install" from the pull-down menu.

  - The Device Manager will show (under "Universal Serial Bus devices") the name and state according to the firmware loaded (e.g. `Hantek 6022BE - Loader`, `Hantek 6022BE - OpenHantek`).
  The [PulseView/sigrok-cli](https://sigrok.org/) firmware is also recognized (e.g. `Hantek 6022BE - Sigrok`).

  It is recommended to use the `.inf` file, but it is also possible to alternatively use the [**Zadig**](docs/build.md#microsoft-windows-usb-driver-install-with-zadig) tool
  and follow the good [step-by-step tutorial](docs/OpenHantek6022_zadig_Win10.pdf) provided by [DaPa](https://github.com/DaPa).

## Important!
The scope doesn't store the firmware permanently in flash or eeprom, it must be uploaded after each power-up and is kept in ram 'til power-down.
If the scope was used with a different software (old openhantek, sigrok or the windows software) the scope must be unplugged and replugged one-time before using it with OpenHantek6022 to enable the automatic loading of the correct firmware.
The top line of the program must display the correct firmware version (FW0210).

## Specifications, Features, Limitations and Developer Documentation
I use this project mainly to explore how DSP software can improve and extend the [limitations](docs/limitations.md)
of this kind of low level hardware. It would have been easy to spend a few bucks more to buy a powerful scope - but it would be much less fun :)
Please refer also to the [developer info](docs/developer_info.md).

## Contribute

For contributions to the original cross-platform project, use
[OpenHantek/OpenHantek6022](https://github.com/OpenHantek/OpenHantek6022). Report problems specific to this Intel
macOS modification in this repository rather than sending fork-specific build or calibration reports upstream.

We welcome any reported GitHub issue if you have a problem with this software. Send us a pull request for enhancements and fixes. Some random notes:
   - Read [how to properly contribute to open source projects on GitHub][10].
   - Create a separate branch other than *main* for your changes. It is not possible to directly commit to main on this repository.
   - Write [good commit messages][11].
   - Use the same [coding style and spacing][13] -> install clang-format and use make target: `make format` or execute directly: `clang-format -style=file -i *.cpp *.h`.
   - It is mandatory that your commits are [Signed-off-by:][12], e.g. use git's command line option `-s` to append it automatically to your commit message:
     `git commit -s -m 'This is my good	commit message'`
   - Open a [pull request][14] with a clear title and description.
   - Read [Add a new device](docs/adddevice.md) if you want to know how to add a device.
   - We recommend QtCreator as IDE on all platforms. It comes with CMake support, a decent compiler, and Qt out of the box.

[10]: http://gun.io/blog/how-to-github-fork-branch-and-pull-request
[11]: http://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html
[12]: https://github.com/probot/dco/blob/master/README.md
[13]: http://llvm.org/docs/CodingStandards.html
[14]: https://help.github.com/articles/using-pull-requests

## Other DSO Open Source Software
* [Firmware used by OpenHantek and python bindings for 6022BE/BL](https://github.com/Ho-Ro/Hantek6022API)
* [sigrok](http://www.sigrok.org)

## Other Related Software
* [HScope for Android](https://www.martinloren.com/hscope/) A one-channel basic version is available free of charge (with in-app purchases).

## History
The program was initially developed by [David Gräff and others](https://github.com/OpenHantek/openhantek/graphs/contributors)
on [github.com/OpenHantek/openhantek](https://github.com/OpenHantek/openhantek),
but David [stopped maintaining](https://github.com/OpenHantek/openhantek/issues/277) the programm in December 2018 and I took over. 

<!--
## Donate for FSF
If you really enjoy this project and would like to donate, please give it to the [Free Software Foundation](https://www.fsf.org/).
Without the FSF, we wouldn't have this [free software](https://www.gnu.org/philosophy/free-sw.html) that we can use today.
-->

# Please Help the Victims of the War!
**Openhantek6022** is a project where people from all over the world collaborate peacefully, regardless of where they live.
If you are lucky enough to live in peace, please [**donate**](https://www.icrc.org/en/donate/ukraine) 
to the *International Committee of the Red Cross*.

![blue-yellow](docs/images/blue-yellow.png)
