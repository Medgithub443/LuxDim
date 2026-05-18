# LuxDim 💡

A lightweight and high-performance utility for Windows to control monitor brightness directly from your desktop. Built with efficiency in mind, LuxDim talks directly to your monitor using DDC/CI via the Windows API.

<img width="112" height="275" alt="image" src="https://github.com/user-attachments/assets/e5ff76ea-7c8b-4c55-b9e1-7d106263df37" />
<img width="149" height="111" alt="image" src="https://github.com/user-attachments/assets/d8f342b7-dc01-464a-a915-4b09359b4d58" />
<img width="287" height="128" alt="image" src="https://github.com/user-attachments/assets/52d74140-9c8d-49e1-99d1-d0fce2cee979" />




## Features
- **Tray:** Utility works in tray.
- **Hotkeys:** You can configure your own hotkeys to change brightness.
- **Ultra-lightweight:** The executable is under 300 KB.
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
3. Run the installer and follow the instructions.
4. (Optional) Check the "Launch on Windows startup" box during installation to keep the utility active.

## Technical Details
- **Compiler:** GCC 15.2.0 (WinLibs build)
- **Environment:** UCRT (Universal C Runtime)
- **Libraries used:** `dxva2`, `user32`, `gdi32`, `shell32`, `comctl32`
- **Linking:** Static (`-static`)

## License
MIT License
