# Smooth Directions

An SKSE plugin that smooths walk direction transitions in Skyrim.

## The Problem

In vanilla Skyrim, there are noticeable sharp transitions between animations when changing walking directions. The character "snaps" to the new direction instantly, which looks unnatural.

## The Solution

This plugin intercepts the `Direction` animation graph variable and smoothly interpolates it when the player (and optionally NPCs) is walking. This creates fluid, natural-looking direction changes instead of abrupt snaps.

**Only affects walking animations** - Running and sprinting are unaffected to maintain responsiveness during fast movement.

## Features

- Smooth direction transitions when walking
- Configurable smoothing speed via INI file
- Optional NPC support (disabled by default for performance)
- Handles angle wraparound correctly (e.g., 350° to 10°)
- Supports sneaking as well as regular walking

## Installation

1. Install [SKSE64](https://skse.silverlock.org/) if you haven't already
2. Copy `SmoothDirections.dll` to `Data/SKSE/Plugins/`
3. (Optional) Copy `SmoothDirections.ini` to `Data/SKSE/Plugins/` to customize settings

## Configuration

Create or edit `Data/SKSE/Plugins/SmoothDirections.ini`:

```ini
[General]
; Enable for player (default: true)
bEnableForPlayer=true

; Enable for NPCs (default: false, may impact performance)
bEnableForNPCs=false

[Smoothing]
; Smoothing speed (1.0 to 20.0, default: 8.0)
; Lower = smoother but slower transitions
; Higher = faster but potentially less smooth
fSmoothingSpeed=8.0

; Minimum speed threshold (default: 0.1)
fMinSpeedThreshold=0.1
```

## Building

### Requirements

- Visual Studio 2022
- CMake 3.21+
- vcpkg (with `VCPKG_ROOT` environment variable set)

### Build Steps

1. Clone the repository with submodules:
   ```bash
   git clone --recursive https://github.com/your-repo/SmoothDirections.git
   ```

2. Open in Visual Studio or your preferred IDE with CMake support

3. Build using the appropriate CMake preset

## Technical Details

The plugin works by:
1. Hooking the `Actor::Update` virtual function
2. Reading the current `Direction` animation graph variable
3. Smoothly interpolating from the current displayed direction to the target direction
4. Writing the smoothed value back to the animation graph

The smoothing only activates when the actor is walking (not running or sprinting), ensuring combat responsiveness is not affected.

## Compatibility

- Skyrim SE, AE, and VR (via CommonLibSSE-NG)
- Should be compatible with most animation mods
- Load order independent

## Credits

- Built using [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR)
- Based on the [SKSE Plugin Template](https://github.com/SkyrimScripting/SKSE_Templates)

## License

MIT License - See LICENSE file for details
