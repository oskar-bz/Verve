# Verve theme library

Ready-to-load `.vvtheme` files. Each is plain text — one `field r g b` line per
colour, plus `radius` and `font_size` — the format written by `vv_theme_save`
and read by `vv_theme_load` (see `include/verve/vv_theme.h`).

| File | Look |
| --- | --- |
| `verve-dark.vvtheme` | Verve's default dark palette |
| `verve-light.vvtheme` | Clean neutral light |
| `windows-classic.vvtheme` | Classic Win32 (9x/2000) silver 3D chrome, square corners |
| `winui-light.vvtheme` | WinUI / Fluent — Windows 11 light |
| `winui-dark.vvtheme` | WinUI / Fluent — Windows 11 dark |
| `adwaita-light.vvtheme` | GNOME Adwaita light |
| `adwaita-dark.vvtheme` | GNOME Adwaita dark |
| `nord.vvtheme` | Nord (cool-toned dark) |
| `solarized-light.vvtheme` | Solarized light |

These mirror the built-in constructors in `vv_theme.h` (`vv_theme_dark`,
`vv_theme_win32`, `vv_theme_adwaita`, …), exposed as the `vv_theme_presets[]`
table. Load one at runtime:

```c
vv_Theme t = vv_theme_dark();          // sensible base for partial files
if (vv_theme_load(&t, "themes/nord.vvtheme"))
    vv_set_theme(&t);                  // the whole UI springs to it
```

Open any of them in the theme editor (`make gui && ./build/theme_editor`,
File ▸ Open theme), or pick a built-in from the Preset dropdown.
