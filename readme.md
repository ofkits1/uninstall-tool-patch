# Uninstall Tool Patch

A lightweight DLL proxy that removes trial limitations from Uninstall Tool.

## What It Does

- Bypasses license and serial verification checks
- Removes "UNREGISTERED VERSION" text from the UI
- Hides "Trial Expired" and "Trial Reminder" popups
- Exe stays untouched — patches applied in-memory at runtime

## How It Works

The DLL masquerades as `msimg32.dll` and sits alongside `UninstallTool.exe`. On launch it:

1. Verifies the host EXE via hash before applying patches
2. Patches license verification functions to force registered state
3. Hooks `SetWindowTextW`, `SendMessageW`, and `CreateWindowExW` to sanitize trial text and hide nag windows

## Installation

1. Drop msimg32.dll (compiled from this source) in the install folder
2. Run Uninstall Tool.exe — that's it

## Notes

- Built for **Uninstall Tool x64** only. Other builds may not work.
- AV may flag the DLL — false positives are common with in-memory patching tools.
- For educational purposes only.

## Disclaimer

This project is for **educational and research purposes only**. Use at your own risk. The author is not responsible for any misuse or damage.

---

Cracked by **github.com/ofkits1**
