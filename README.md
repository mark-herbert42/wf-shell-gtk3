# wf-shell-gtk3

wf-shell-gtk3 is cloned from main wf-shell repo at the pint of wf-shell migration to gtk4.

The purpose of this project is to create lightweight robust panel with multi-output support and deep wayfire integration 
to be used in MATE desktop in addition to main mate-panel or as a replacement, and simple and fast wallpaper app to
replace more heavy caja functionality  combining wallpper and desktop icons fopr those who do not want icons.

Changes done so far

Backported some usefull config options - sizes, spacing etc.
Backported language widget to control/show keyboard layout
Added right-click option to battery widget to open mate-power-statistic dialog.
Added right-clock optionb to volume control to open mate-mixer app.
Removed dock application to make things simple - we target panel and background only.
Removed network widget. It is too far from nm-applet and bluez applet functionality whch integrates perfectly
Added right-click option on launcher to remove launcher from launchers plugin
Added right-click option on the menu item to add menu item to launchers.

# Dependencies

wf-shell needs the core wayland libraries and protocols (`wayland-devel` and `wayland-protocols-devel` for Fedora), gtkmm-3.0 and [wf-config](https://github.com/WayfireWM/wf-config)

# Build

Just like any meson project:
```
git clone https://github.com/WayfireWM/wf-shell && cd wf-shell
meson build --prefix=/usr --buildtype=release
ninja -C build && sudo ninja -C build install
```

# Configuration

To configure the panel and the dock, wf-shell uses a config file located (by default) in `~/.config/wf-shell.ini`
An example configuration can be found in the file `wf-shell.ini.example`, alongside with comments what each option does.

# Style & Theme

Style and theme can be altered with [CSS](/data/css/)

# Screenshots

![Panel & Background demo](/screenshot.png)
