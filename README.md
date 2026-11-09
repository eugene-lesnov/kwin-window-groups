# Window Groups

Window Groups is a KWin script for KDE Plasma 6 on Wayland. It lets you group windows from different applications in a shared frame and switch between them using a tab bar.

## Using Window Groups

Hold **Meta** and drag a window with the left mouse button toward the top edge of another window. Release over the highlighted drop zone to group them. You can drag more windows into an existing group.

Click a tab or press **Meta+Page Up** or **Meta+Page Down** to switch windows. Right-click a tab to detach its window or disband the group.

## How it works

The script uses KWin's standard window move gesture. QML and JavaScript handle the tab bar, drop indicator, and group state. A native C++ module detects the drag and release, positions the active window inside the group frame, and hides inactive windows until selected.

## Compatibility

The project targets Plasma 6 Wayland. The current build requires **KWin 6.7.6** and **Qt 6.10 or later**. The native module uses KWin internals, so using a different KWin version requires checking API compatibility and rebuilding against matching headers.

**Experimental. Use at your own risk.**
