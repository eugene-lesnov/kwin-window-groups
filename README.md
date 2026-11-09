# Window Groups

Keep your editor and terminal in one frame, with a tab for each window. The group moves and resizes as one window and gets one entry in the taskbar. Window Groups is an experimental KWin script for KDE Plasma 6 on Wayland.

## Using it

Hold **Meta** and drag a window with the left mouse button toward the top edge of another window. Let go over the highlighted zone. The same gesture adds a window to an existing group. To group by keyboard, press **Meta+G** in one window, focus the other, and press it again.

- Click a tab to switch windows.
- Drag tabs along the bar to reorder them. To detach one, pull it away from the bar and let go when the preview appears. **Meta+Shift+G** detaches the active tab.
- Click a tab's **×** to close its window. The right-click menu lets you detach a tab or ungroup all the windows.

## Build and install

The native module uses KWin internals and needs **KWin 6.7.6** with matching development files. You'll also need Qt 6.10+, CMake 3.21+, a C++23 compiler, pkg-config, KF6 I18n and Service, and the Wayland client headers, scanner, and protocols.

Run this from the repository root.

```sh
build_dir=$(mktemp -d) &&
    cmake -S . -B "$build_dir" &&
    cmake --build "$build_dir" &&
    cmake --install "$build_dir" --prefix "$PWD" &&
    kpackagetool6 --type KWin/Script --install "$PWD"
```

Enable **Window Groups** in **System Settings → Window Management → KWin Scripts**. If Window Groups is already installed, use `--upgrade` in the last command.
