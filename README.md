# Iron Veil

An original retro first-person shooter written in **C11** for **Windows 10/11**. Explore a hostile facility, collect three power cores, defeat its Warden, and activate the southeast exit. The source, a compiled executable, and launchers are included.

## Play

Double-click **Play.bat**, or run **build/IronVeil.exe**. No installation, external game assets, accounts, or third-party runtime downloads are required. Keep the executable in `build/` for the documented diagnostic output paths.

| Action | Controls |
| --- | --- |
| Move forward / backward | W / S, Up / Down |
| Strafe left / right | A / D |
| Turn | Left / Right arrow keys |
| Toggle mouse look | F1 |
| Fire | Space, left mouse button, or J |
| Reload | R |
| Open a nearby door / activate exit | E |
| Sprint | Shift |
| Automap | M or Tab |
| Pause / resume / close map | Escape |
| Begin / restart after death or victory | Enter |
| Quit from pause | Q |

Hold Space or the mouse button for repeated fire. A magazine holds 12 rounds, and an empty magazine automatically starts reloading when you try to fire. Reloading takes a little over a second. Pick up medkits and ammo by walking over them. Medkits remain available if health is already full.

The automap reveals nearby terrain and marks all remaining power cores in teal. Visible enemies appear red; doors and the exit are gold. Find all three cores, defeat the red Warden in the southeast, then stand near the exit beacon and press E. The Warden fires more quickly and launches two projectiles below half health. Moving sideways helps dodge shots.

Map and pause screens freeze gameplay. Switching away from the window pauses the game and releases mouse look. The window can be resized and uses letterboxing. This is one short, self-contained level; there is no save system or audio.

## Renderer

This is a **2.5D software raycaster**, in the tradition of early first-person games. It uses a square tile map, a fixed-height viewpoint, textured walls, perspective floors and ceilings, and depth-tested billboard enemies. It does not implement Doom's sector engine or general-purpose physically based ray tracing. All artwork, level design and names are original.

Every frame is drawn into a 480-by-300 pixel software framebuffer. Win32/GDI scales that image into an off-screen client bitmap and presents the finished frame in one copy, avoiding clear-before-draw flickering. No OpenGL, DirectX, SDL, or GPU renderer is required.

## Build

Run **Build.bat** or `build.ps1`. The script finds MinGW-w64 GCC on PATH or in a standard MSYS2 installation. It builds C11 with optimization and warnings enabled, and statically links the GCC runtime. Only standard Windows system DLLs are needed at runtime.

```powershell
.\build.ps1
```

Optional CMake build with MinGW and `mingw32-make` available on PATH:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

This edition targets Windows. The native window host would need replacing for Linux or macOS.

## Read and verify

See [CODE_GUIDE.md](CODE_GUIDE.md) for the renderer and gameplay walkthrough. `.clang-format` keeps source formatting consistent.

```powershell
$process = Start-Process -FilePath .\build\IronVeil.exe -ArgumentList '--self-test' -WindowStyle Hidden -Wait -PassThru
$process.ExitCode
Get-Content .\self-test-results.txt
```

The checks cover movement, collision, ray distances, doors, shooting and wall occlusion, reloads, pickups, damage protection, boss attacks, projectile collisions, death, victory, and routes to every item, enemy, and the exit. Test mode also renders BMP previews in `screenshots/`. It uses no player save files or cloud services.
