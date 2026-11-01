Project Blue Moon for Windows
=============================

A native PC port of TSUKIHIME -A piece of blue glass moon- (PS4). This is an Alpha: the game is
playable up to the title of chapter 1, in English. It has been tested under Wine on Linux, not yet
on a real Windows PC: if something fails, please report it (see Problems).

No game files are included. You need your own dump of TSUKIHIME -A piece of blue glass moon-:
CUSA47999, version 01.00, decrypted, as a folder that contains eboot.bin, sce_module and sce_sys.

Requirements
- Windows 10 (1903 or newer) or Windows 11, 64-bit.
- A 64-bit processor with AVX: Intel Sandy Bridge (2011) or newer, AMD FX or newer. Sandy/Ivy
  Bridge and AMD FX run with emulated instructions (an "ISA:" log line; speed not measured).
  Older processors stop at start with a "STOP: this CPU lacks" line in the log.
- A graphics card with Vulkan 1.3 and an up-to-date driver.
  Nothing else: Python and the libraries are inside this folder.

Starting
1. Extract this folder to a place you can write to, for example
   C:\Games\project-blue-moon-windows (not inside the zip, and not in Program Files: saves and
   settings are written next to the program).
2. Open "Project Blue Moon.exe". If Windows shows "Windows protected your PC", click More info,
   then Run anyway: the program is not digitally signed.
3. Click "Choose the game folder", then Browse... next to Game folder, and select the folder
   with eboot.bin. A folder named CUSA47999 next to this folder is found on its own.
4. Back on Home, the game's cover and "CUSA47999 · version 01.00" appear. Click PLAY. The first
   start takes a little longer while the game image is prepared.
5. With no save, the game opens with the prologue video (about 6 and a half minutes), which
   buttons do not skip. After the first save it starts at the title menu.

While playing
- Insert on the keyboard, or R3 + L2 on the controller, opens the port's menu: image, performance
  and the performance overlay.
- The mouse moves the game's own cursor.
- Keyboard: WASD left stick, arrows right stick, Space Cross, Left Shift Circle, E Square,
  Q Triangle, 1/3 L1/R1, R/F L2/R2, Z/C L3/R3, I/K/J/L d-pad, Enter Options, Tab touchpad click.
  Change them on the launcher's Game page, under Controls.

Next time
- "Play Project Blue Moon.exe" starts the game straight away with the saved settings.
- Advanced -> Desktop shortcut makes a shortcut; Advanced -> Add to Steam adds the game to Steam
  (close Steam first).

Data
- Saves, shader cache and the log: user\ next to Project Blue Moon.exe (keep it when updating).
- Game settings: tsuki.ini next to Project Blue Moon.exe; launcher options in
  %APPDATA%\tsuki-launcher.
- Generated files (the prepared game image): out\.

Problems
- Black screen at start: Advanced -> Clear shader cache, then start again.
- The game does not open or stops: look at the Log section on Advanced, and send
  user\last_run.log with your report: https://github.com/Waffle631/Project-Blue-Moon/issues
- RTSS/Afterburner and ReShade must target bin\tsuki-probe.exe (the game), not the launcher.
  Select Vulkan in the ReShade installer.

This project is not affiliated with TYPE-MOON, Aniplex or Sony Interactive Entertainment.
TSUKIHIME is a work of TYPE-MOON; the PlayStation 4 edition is published by Aniplex.
