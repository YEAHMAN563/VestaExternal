# Vesta 1.2.5 - Custom Modifications

## Changes Made

### 1. Fixed Menu Toggle Issue (CRITICAL FIX)
**File:** `src/render/menu/menu.cpp` & `src/render/menu/menu.hpp`
**Problem:** Menu was opening and closing repeatedly without pressing the toggle key
**Root Cause:** 
- The `captured_down` variable was mixing ImGui's key state with GetAsyncKeyState
- ImGui's IsKeyDown() could report the key as pressed even after physical release
- This created a feedback loop causing rapid toggling
**Solution:** 
- Removed ImGui key state checking entirely from toggle detection
- Use only GetAsyncKeyState for reliable physical keyboard input
- Added 100ms debounce timer to prevent any rapid toggling
- Simplified edge detection logic to prevent race conditions
- The menu now only toggles on actual key press events

### 2. Added DEL Key as Alternative Menu Toggle
**File:** `src/render/menu/menu.cpp`
**Enhancement:** 
- Added VK_DELETE (DEL key) as an additional menu toggle key
- Works alongside the configured menu key (default: INSERT)
- Both keys can now be used to open/close the menu

### 3. Improved Overlay FPS
**File:** `src/config/misc.hpp`
**Changes:**
- Disabled FPS limiter by default (`limit_fps = false`)
- Increased maximum FPS limit from 240 to 500
- This allows the overlay to run at higher frame rates for smoother rendering

## Technical Details

### Menu Toggle Fix Details
The issue was caused by three interacting problems:
1. **ImGui State Interference**: `ImGui::IsKeyDown()` was being used alongside `GetAsyncKeyState()`, causing conflicting key state readings
2. **Captured Key State**: The `captured_down` variable tried to "latch" the key state when menu was open, but this prevented proper release detection
3. **Toggle Logic**: The original code could flip-flop if called multiple times

The fix ensures:
- Clean, single-source keyboard input detection with proper edge detection
- 100ms minimum time between toggles (debounce protection)
- Proper state management preventing race conditions

### FPS Improvements
By disabling the FPS limiter by default and increasing the maximum limit, the overlay can now:
- Render at monitor refresh rate (144Hz, 240Hz, etc.)
- Provide smoother visual feedback
- Reduce input latency

## Build Information
- **Built with:** Visual Studio 2026 (v145 toolset)
- **Platform:** x64 Release
- **Output:** `build/release/bin/vesta.exe` (8.5 MB)
- **Build Date:** September 27, 2026
- **Final Build Time:** 1:33 PM (with debounce protection)

## Usage Notes
1. Press **INSERT** or **DEL** to toggle the menu
2. FPS limiter is disabled by default - you can re-enable it in settings if needed
3. If you experience performance issues, enable the FPS limiter in Settings > Engine
4. The 100ms debounce prevents any rapid flickering completely

## Files Modified
1. `src/render/menu/menu.cpp` - Menu toggle logic (CRITICAL FIX + debounce)
2. `src/render/menu/menu.hpp` - Added debounce timer member
3. `src/config/misc.hpp` - FPS limiter defaults
