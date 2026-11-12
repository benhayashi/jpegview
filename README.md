[![Documentation](https://img.shields.io/badge/Docs-Outdated-yellowgreen)](https://htmlpreview.github.io/?https://github.com/sylikc/jpegview/blob/master/src/JPEGView/Config/readme.html) [![Localization Progress](https://img.shields.io/badge/Localized-91%25-blueviolet)](#Localization) [![Build x64](https://github.com/sylikc/jpegview/actions/workflows/build-release-x64.yml/badge.svg?branch=master)](https://github.com/sylikc/jpegview/actions/workflows/build-release-x64.yml) [![OS Support](https://img.shields.io/badge/Windows-XP%20%7C%207%20%7C%208%20%7C%2010%20%7C%2011-blue)](#) [![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue)](https://github.com/sylikc/jpegview/blob/master/LICENSE.txt)

[![Latest GitHub Release](https://img.shields.io/github/v/release/sylikc/jpegview?label=GitHub&style=social)](https://github.com/sylikc/jpegview/releases)[![Downloads](https://badgen.net/github/assets-dl/sylikc/jpegview?cache=3600&color=grey&label=)](#) [![WinGet](https://repology.org/badge/version-for-repo/winget/jpegview.svg?allow_ignored=1&header=WinGet)](https://winstall.app/apps/sylikc.JPEGView) [![PortableApps](https://img.shields.io/badge/PortableApps-Current-green)](https://portableapps.com/apps/graphics_pictures/jpegview_portable) [![Scoop](https://repology.org/badge/version-for-repo/scoop/jpegview-fork.svg?header=Scoop)](https://scoop.sh/#/apps?q=%22jpegview-fork%22) [![Chocolatey](https://img.shields.io/chocolatey/v/jpegview)](https://community.chocolatey.org/packages/jpegview) [![Npackd](https://repology.org/badge/version-for-repo/npackd_stable/jpegview.svg?allow_ignored=1&header=Npackd)](https://www.npackd.org/p/jpegview)

# JPEGView - Modern Lightweight Photo Viewer & HDR Engine

A modernized, ultra-fast, and lightweight image viewer and editor for Windows devices.

This fork preserves JPEGView's legendary sub-10ms startup time, minimal resource footprint, and keyboard/mouse-driven navigation, while introducing native HDR display capabilities, modern image format pipelines (JXL, HEIC, HIF, AVIF, WebP), seamless Windows 11 default app integration, and an uncompromising offline privacy guarantee.

## Key Features

### 🌟 Native HDR Display & Advanced Color
- **Direct3D 11 / DXGI 1.5+ scRGB Pipeline**: Real HDR display output utilizing an FP16 (`DXGI_FORMAT_R16G16B16A16_FLOAT`) scRGB swapchain with automatic monitor peak luminance detection via `IDXGIOutput6`.
- **HDR on HDR Displays**: Automatically detects HDR-capable monitors and renders high-dynamic-range photos (HEIC/HIF 10/12-bit PQ/HLG, JPEG XL HDR, AVIF 10-bit) in true high dynamic range.
- **Intelligent SDR Tone Mapping**: View HDR photos on SDR monitors or standard color profiles using tone mapping curves (BT.2390 Hermite spline knee, Reinhard, ACES filmic).
- **Instant Hotkey Toggle (`H`)**: Toggle between true HDR and tone-mapped SDR with a single keypress. Real-time filename indicators show `[HDR]` or `[HDR->SDR]`.

### 🚀 Zero Load Time & Ultra-Lightweight
- **Sub-10ms Startup**: Opens instantly without delay.
- **On-Demand Delay Loading**: Codec DLLs (libjxl, libheif, libavif, libraw) and DirectX rendering subsystems (`d3d11.dll`, `dxgi.dll`) are loaded strictly on-demand. Standard SDR viewing has zero DirectX overhead.
- **Pure Native C++**: No .NET runtimes, Electron, or web views.

### 🖼️ Comprehensive Modern Format Support
- **Modern Formats**: HEIC, HEIF, HIF (10/12-bit HDR, Apple/Canon/Sony HDR), JPEG XL (`.jxl`), AVIF, WebP, QOI.
- **Standard & Lossless**: JPEG, PNG (with APNG animations), GIF, BMP, TIFF, TGA.
- **Creative & Professional**: Photoshop PSD and PSB (large format).
- **Camera RAW**: Comprehensive LibRaw support for modern cameras including Canon (CR2, CR3), Nikon (NEF), Sony (ARW), Fujifilm (RAF), Adobe DNG, and more.

### ⚙️ Windows 10 & 11 Default Photo Viewer
- **1-Click Default App Integration**: Deep-links directly to Windows 10/11 Default Apps (`ms-settings:defaultapps?appName=JPEGView`).
- **Full Shell Capabilities**: Registered under `HKLM\SOFTWARE\RegisteredApplications` with capabilities for seamless association without Windows hash warnings.
- **Available as Portable (.exe) and MSI Installer**: Ready for individual use or enterprise system-wide deployment.

### 🔒 100% Offline & Zero Telemetry
- **Complete Privacy**: No telemetry, analytics, telemetry tracking, or external network connections of any kind. 100% offline and self-contained.

### 🎨 Built-in Real-time Processing & Navigation
- **Responsive Controls**: Arrow keys to navigate, mouse scroll wheel to zoom, left drag to pan.
- **Real-time Processing**: Fast sharpness, contrast, color balance, local density correction, rotation, and perspective adjustments.
- **Movie/Slideshow Mode**: Play folders of images sequentially.

# Installation

## Official Releases

Official releases will be made to [sylikc's GitHub Releases](https://github.com/sylikc/jpegview/releases) page.  Each release includes:

* **Archive Zip/7z** - Portable
* **Windows Installer MSI** - For Installs
* **Source code** - Build it yourself

## Portable

JPEGView _does not require installation_ to run.  Just **unzip, and run** either the 64-bit version, or the 32-bit version depending on which platform you're on.  It can save the settings to the extracted folder and run entirely portable.

## MSI Installer

For those who prefer to have JPEGView installed for All Users, a 32-bit/64-bit installer is available to download starting with v1.0.40.

(Unfortunately, I don't own a code signing certificate yet, so the MSI release is not signed.  Please verify checksums!)

### WinGet

If you're on Windows 11, or Windows 10 (build 1709 or later), you can also download it directly from the official [Microsoft WinGet tool](https://docs.microsoft.com/en-us/windows/package-manager/winget/) repository.  This downloads the latest MSI installer directly from GitHub for installation.

Example Usage:

C:\> `winget search jpegview`
```
Name     Id              Version  Source
-----------------------------------------
JPEGView sylikc.JPEGView 1.1.43  winget
```

C:\> `winget install jpegview`
```
Found JPEGView [sylikc.JPEGView] Version 1.1.43
This application is licensed to you by its owner.
Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Downloading https://github.com/sylikc/jpegview/releases/download/v1.1.43/JPEGView64_en-us_1.1.43.msi
  ██████████████████████████████  4.23 MB / 4.23 MB
Successfully verified installer hash
Starting package install...
Successfully installed
```

## PortableApps

Another option is to use the official [JPEGView on PortableApps](https://portableapps.com/apps/graphics_pictures/jpegview_portable) package.  The PortableApps launcher preserves user settings in a separate directory from the extracted application directory.  This release is signed.

## Scoop

[Scoop](https://scoop.sh/) is a Windows command-line installer and manager for portable applications.

To install with Scoop, run the following commands:

```shell
scoop bucket add extras
scoop install extras/jpegview-fork
```

After installation, the configuration file is located at `%UserProfile%\scoop\persist\JPEGView-fork\JPEGView.ini`.

## System Requirements

* 64-bit version: Windows 7/8/10/11 64-bit or later
* 32-bit version: Windows 7 or later
  * A special _32-bit Windows XP SP2_ build is available, which supports most formats (except for formats added after v1.0.37.1, ex. Animated PNG, JXL, HEIC).  Other features and options are the same as the normal builds.

## What's New

* See what has changed in the [latest releases](https://github.com/sylikc/jpegview/releases)
* Or Check the [CHANGELOG.txt](https://github.com/sylikc/jpegview/blob/master/CHANGELOG.txt) to review new features in detail.

# Localization

By default, the language is auto-detected to match your Windows Locale.  All the text in the menus and user interface should show in your language.  To override the auto-detection, manually set `Language` option in `JPEGView.ini`

JPEGView is currently translated/localized to 28 languages:

| INI Option | Language |
| ---------- | -------- |
| be | Belarusian |
| bg | Bulgarian |
| cs | Czech |
| de | German |
| el | Greek, Modern |
| es-ar | Spanish (Argentina) |
| es | Spanish |
| eu | Basque |
| fi | Finnish |
| fr | French (Français) |
| hu | Hungarian |
| it | Italian |
| ja | Japanese (日本語) |
| ko | Korean (한국어) |
| pl | Polish |
| pt-br | Portuguese (Brazilian) |
| pt | Portuguese |
| ro | Romanian |
| ru | Russian (Русский) |
| sk | Slovak |
| sl | Slovenian (Slovenščina) |
| sr | Serbian (српски) |
| sv | Swedish |
| ta | Tamil |
| tr | Turkish (Türkçe) |
| uk | Ukrainian (Українська) |
| zh-tw | Chinese, Traditional (繁體中文) |
| zh | Chinese, Simplified (简体中文) |

See the [Localization wiki page](https://github.com/sylikc/jpegview/wiki/Localization#localization-status) for translation status for each language.

# Help / Documentation

The JPEGView documentation is a little out of the date at the moment, but should still give a good summary of the features.

This [readme.html](https://htmlpreview.github.io/?https://github.com/sylikc/jpegview/blob/master/src/JPEGView/Config/readme.html) is part of the JPEGView package.

# Brief History

This GitHub repo continues the legacy (is a "fork") of the excellent project [JPEGView by David Kleiner](https://sourceforge.net/projects/jpegview/).  Unfortunately, starting in 2020, the SourceForge project has essentially been abandoned, with the last update being [2018-02-24 (1.0.37)](https://sourceforge.net/projects/jpegview/files/jpegview/).  It's an excellent lightweight image viewer that I use almost daily!

The starting point for this repo was a direct clone from SourceForge SVN to GitHub Git.  By continuing this way, it retains all previous commits and all original author comments.

I'm hoping with this project, some devs might help me keep the project alive!  It's been awhile, and could use some new features or updates.  Looking forward to the community making suggestions, and devs will help with some do pull requests as some of the image code is quite a learning curve for me to pick it up. -sylikc

## Special Thanks

Special thanks to [qbnu](https://github.com/qbnu) for adding additional codec support!
* Animated WebP
* Animated PNG
* JPEG XL with animation support
* HEIF/HEIC/AVIF support
* QOI support
* ICC Profile support for WebP, JPEG XL, HEIF/HEIC, AVIF
* LibRaw support (all updated RAW formats, such as CR3)
* Photoshop PSD support

Thanks to all the _translators_ which keep JPEGView strings up-to-date in different languages!  See [CHANGELOG.txt](https://github.com/sylikc/jpegview/blob/master/CHANGELOG.txt) to find credits for translators at each release!
