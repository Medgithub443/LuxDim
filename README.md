# LuxDim 💡

A lightweight and high-performance utility for Windows to control monitor brightness directly from your desktop. Built with efficiency in mind, LuxDim talks directly to your monitor using DDC/CI via the Windows API.

<img width="112" height="275" alt="image" src="https://github.com/user-attachments/assets/e5ff76ea-7c7b-4c55-b9e1-7d106263df37" />
<img width="149" height="111" alt="image" src="https://github.com/user-attachments/assets/d8f342b7-dc01-464a-a915-4b09359b4d58" />
<img width="287" height="128" alt="image" src="https://github.com/user-attachments/assets/52d74140-9c8d-49e1-99d1-d0fce0cee979" />

## What's New in 1.3-beta
- **OSD overlay:** a large light-blue brightness number appears in the top-right corner whenever you change brightness with a hotkey, then smoothly fades out. The overlay window is transparent and click-through — it never gets in your way.
- **Bilingual installer:** the Inno Setup installer speaks Russian or English, matching your Windows language automatically. It offers a desktop icon and a "Launch at Windows startup" task.
- **Fixed settings window:** the Save/Cancel buttons are no longer clipped.
- **Fixed popup layout:** the brightness slider is now perfectly centered in the popup.
- **Static tray icon:** removed per-brightness icon recoloring — one clean, standard icon (smaller code, less work per brightness change).

## Features
- **Tray:** Utility works in tray.
- **Hotkeys:** You can configure your own hotkeys to change brightness.
- **OSD:** On-screen brightness indicator on hotkey changes.
- **Ultra-lightweight:** The executable is under 350 KB.
- **Stand-alone:** No external dependencies required (Static linking).
- **Fast:** Direct hardware communication with minimal CPU usage.
- **Easy Setup:** Comes with a multi-language installer and auto-start option.

## Compatibility 💻

| OS Version | Compatibility | Notes |
| :--- | :--- | :--- |
| **Windows 10 / 11** | ✅ Full Support | Recommended for the best experience (UCRT-based). |
| **Windows 7 / 8 / 8.1** | ⚠️ Partial | Requires [Universal C Runtime](https://microsoft.com). |
| **Hardware** | DDC/CI Support | Your monitor must support DDC/CI (most modern monitors do). |

## Installation 🚀
1. Go to the [Releases](https://github.com) page.
2. Download `LuxDim_Setup.exe`.
3. Run the installer and follow the instructions (Russian/English is selected automatically).
4. (Optional) Check the "Launch at Windows startup" box during installation to keep the utility active.

## Technical Details
- **Compiler:** GCC 16.2.0 (w64devkit)
- **Environment:** UCRT (Universal C Runtime)
- **Libraries used:** `dxva2`, `user32`, `gdi32`, `shell32`, `comctl32`
- **Linking:** Static (`-static`)
- **Installer:** Inno Setup 6 (bilingual RU/EN)

## Building from source
```
g++ -O2 -mwindows -static -static-libgcc -static-libstdc++ ^
    luxdim.cpp -o luxdim.exe ^
    -ldxva2 -luser32 -lgdi32 -lshell32 -lcomctl32
```

## License
MIT License
