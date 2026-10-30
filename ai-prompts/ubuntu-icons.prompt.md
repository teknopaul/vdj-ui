# Fixing an application icon in the Ubuntu dock / task switcher

The icon is resolved by name from the desktop entry, not from the binary.

1. `/usr/share/applications/<app>.desktop` sets `Icon=<name>` (and usually `StartupWMClass=<app>` for window grouping).
2. `<name>` is looked up in the icon theme, e.g.
   `/usr/share/icons/hicolor/<size>/apps/<name>.png` or `.../scalable/apps/<name>.svg`.
3. GTK apps may also call `gtk_window_set_icon_name(window, "<name>")`, which resolves the same way.

## Checklist

```bash
# what does the launcher ask for?
grep -H '^Icon=' /usr/share/applications/<app>.desktop

# what is actually installed?
ls -l /usr/share/icons/hicolor/*/apps/<name>.*

# what is the running window called?
xprop -id "$(xdotool getactivewindow)" WM_CLASS WM_NAME
```

## Rules that save time

- Install PNGs at several sizes (`64x64`, `128x128`, `256x256`, `512x512`); a `scalable` SVG wins over PNGs, so delete it if it is the wrong artwork.
- Refresh caches after changing files:
  ```bash
  sudo update-desktop-database
  
  
  ```
- Restart the app. If the dock still shows the old icon, unpin/re-pin it or log out and back in (GNOME caches launcher icons).
- In `.deb` packages, dpkg does **not** delete files that disappeared from a newer version. `postinst` must remove obsolete icon paths, then refresh the cache. That is why `deploy/DEBIAN/postinst` here deletes the old `48x48` PNG and `scalable` SVG.
- Relative-path launches (`file://` URLs) make no difference to the icon; the `.desktop` `Icon=` name does.
- The window title is separate from the icon: set it with `gtk_window_set_title()`; set the icon with `gtk_window_set_icon_name()` / the `.desktop` file.
