---
title: Download
description: Installers of the latest Mirab release for Windows, macOS and Linux.
hide:
  - navigation
---

# Download Mirab

<p class="mirab-release" data-mirab-release>The installers of the latest release are on the <a href="https://github.com/ehsanMa86/Mirab/releases/latest">release page</a>.</p>

<div class="grid cards mirab-downloads" markdown>

-   :fontawesome-brands-windows: **Windows**

    ---

    Windows 10 and 11, 64-bit.

    [Installer (.exe)](https://github.com/ehsanMa86/Mirab/releases/latest){ .md-button .md-button--primary data-asset="-windows-x64.exe" }
    [Portable (.zip)](https://github.com/ehsanMa86/Mirab/releases/latest){ .md-button data-asset="-windows-x64.zip" }

-   :fontawesome-brands-apple: **macOS**

    ---

    Apple silicon (M1 and later) or Intel.

    [Apple silicon (.dmg)](https://github.com/ehsanMa86/Mirab/releases/latest){ .md-button .md-button--primary data-asset="-macos-arm64.dmg" }
    [Intel (.dmg)](https://github.com/ehsanMa86/Mirab/releases/latest){ .md-button data-asset="-macos-x64.dmg" }

-   :fontawesome-brands-linux: **Linux**

    ---

    x86_64 distributions.

    [AppImage](https://github.com/ehsanMa86/Mirab/releases/latest){ .md-button .md-button--primary data-asset="-linux-x86_64.AppImage" }
    [Archive (.tar.gz)](https://github.com/ehsanMa86/Mirab/releases/latest){ .md-button data-asset="-linux-x64.tar.gz" }

</div>

Earlier versions and the notes of each release are on the [releases page](https://github.com/ehsanMa86/Mirab/releases). What changed is also in the [changelog](changelog.md).

## Installing

- **Windows:** run the installer, or unpack the `.zip` anywhere and start `bin\Mirab.exe`.
- **macOS:** open the `.dmg` and drag Mirab to *Applications*.
- **Linux:** make the AppImage executable (`chmod +x Mirab-*.AppImage`) and run it.

!!! note "The installers are not code-signed yet"
    Windows may show *Windows protected your PC*; choose *More info* and then *Run anyway*. On computers where Smart App Control is on, Windows may refuse to start Mirab at all.
    macOS may say that it cannot check Mirab for malicious software; open *System Settings > Privacy & Security* and choose *Open Anyway* for Mirab.
    Signed installers are planned.

The example models are installed with the application and appear on its start page.

## The SWMM5+ engine

SWMM5+ is an optional engine, published separately by [swmm5plus-capi](https://github.com/ehsanMa86/swmm5plus-capi). Install it from Mirab with *Project > Simulation engines > Install SWMM5+*. Mirab downloads the files for your computer and checks each checksum before it installs them. Offline and scripted installation are described in [Installing SWMM5+](engines.md#installing-swmm5).

## Checking a download

Each release has a `Mirab-<version>-SHA256SUMS.txt` file with the SHA-256 checksum of every installer. Every file also carries a signed build provenance attestation; to confirm that a file was built by the release workflow of the Mirab repository, run this with the [GitHub CLI](https://cli.github.com):

```
gh attestation verify <file> --repo ehsanMa86/Mirab
```

## Building from source

Mirab builds with CMake, a C++17 compiler and Qt 6. See [Building and packaging](building.md).
