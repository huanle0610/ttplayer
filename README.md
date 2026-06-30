# TTPlayer Classic C++ prototype

This directory is the native Win32/CMake rewrite track. The goal is to match the original TTPlayer 5.7 architecture more closely than the Rust/winit prototype:

- one taskbar entry for the main player window
- playlist, lyrics, and equalizer as owned tool windows
- custom painted skin-like windows instead of native child controls
- movable and resizable main and secondary windows
- snap/attach behavior: attached panels move with the main window, detached panels stay independent

The first milestone intentionally uses hand-painted placeholder panels. The next milestone should load original `.skn` packages, parse `Skin.xml`, and render the BMP assets from `C:\RustroverProjects\TTPlayer5.7`.

Build:

```powershell
cmake -S cpp -B cpp/build -G Ninja
cmake --build cpp/build
```
