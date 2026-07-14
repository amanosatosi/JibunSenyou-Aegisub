# Mangetsu Subtitle Renderer

Mangetsu support is implemented as a separate subtitle provider named `Mangetsu`.
It reuses the libassmod-compatible render path, including RGBA rendering and tag
image support, but it is loaded from its own shared library.

The currently supported Mangetsu source is:

https://github.com/amanosatosi/libassmod/tree/mangetsu

## Build And Package

Build/package the `mangetsu` branch of `libassmod` as a shared library, then
place the output beside `aegisub.exe` or the installed Aegisub binary as:

- Windows: `mangetsu.dll`
- Linux: `libmangetsu.so`
- macOS, if supported: `libmangetsu.dylib`

Configure Aegisub packaging with:

```sh
meson setup build -Dwith_mangetsu=true
```

When `mangetsu_path` is omitted, Meson builds the `mangetsu` branch from
`subprojects/libassmod-mangetsu.wrap` and installs it under the platform name
above. Release Windows installer and portable builds enable this, so
`mangetsu.dll` is bundled in shipped artifacts.

To package a prebuilt external library instead, use:

```sh
meson setup build -Dwith_mangetsu=true -Dmangetsu_path=/path/to/mangetsu.dll
```

Use the platform-appropriate path for `mangetsu_path`. Without
`-Dwith_mangetsu=true`, Aegisub still builds and runs, and the Mangetsu provider
is simply unavailable unless a matching library is placed beside the executable
by other packaging.

The libassmod provider is intentionally separate. It only probes:

- Windows: `libassmod.dll`, `assmod.dll`
- Linux: `libassmod.so`
- macOS: `libassmod.dylib`, `libassmod.so`

It does not load plain `ass.dll`, `libass.dll`, `libass.so`, or `libass.dylib`
as libassmod. Built-in `libass` is the fallback provider.

On Windows, libassmod is packaged as `libassmod.dll`. The old `ass.dll` and
`libass.dll` names are obsolete for this fork. Installer upgrades delete those
stale names from the install directory, and the uninstaller removes them as
cleanup in case they came from older Aegisub_Toshi-ban builds.

## Provider UI Behavior

The current preferences UI uses a simple read-only dropdown that cannot disable
individual items. Available subtitle providers appear in the dropdown. Missing
optional providers are shown in an `Unavailable subtitles providers` text
section below it.

A later UI improvement can replace this with real disabled/gray dropdown items.

## Manual Checks

- Select `Mangetsu` in the config while `mangetsu.dll`/`libmangetsu.so` is
  missing; Aegisub should warn once and use `libass`.
- Select `libassmod` while `libassmod.dll`/`libassmod.so` is missing; Aegisub
  should warn once and use `libass`.
- Place only `libass.dll`/`libass.so` beside Aegisub; `Mangetsu` must remain
  unavailable.
- Place only `ass.dll`/`libass.so` beside Aegisub; `libassmod` must remain
  unavailable unless `assmod.dll` or `libassmod.dll`/`libassmod.so` is present.
- When Mangetsu is available, the log should include `Mangetsu loaded from
  <path>`.

## Gradient Editor: fixed video placement

The Mangetsu Gradient Editor has a **Lock Placement** button beside its angle
controls. It changes a primary-fill attached gradient into a fixed-frame
gradient. The angle still comes from the editor's angle control and the colors
and percentage stops still come from the stop editor; only the coordinate space
changes.

Workflow:

```text
1. Open Gradient Editor.
2. Configure the gradient angle and stops.
3. Enable Lock Placement.
4. Drag the desired area on the video.
5. Apply the change.
```

Dragging creates `\pgrd(left,top,right,bottom,angle,stops...)`. The editor
always normalizes the rectangle to `left,top,right,bottom`, stores it in ASS
script coordinates, and keeps it fixed as the video frame changes. It does not
follow `\pos`, `\move`, scaling, or rotation. Pixels outside the rectangle use
the line's normal primary color.

Current Mangetsu support is intentionally limited to the primary RGB fill.
`\pgrd(...)` and `\1pgrd(...)` are equivalent primary-fill aliases; the editor
generates the compact `\pgrd(...)` form. Placement is unavailable for
secondary, outline, shadow, fifth-color, alpha, border, and box gradients. The
button explains this when one of those channels is selected.

Opening an existing placement gradient restores its rectangle, angle, and
stops. Drag once more to replace the rectangle. Disabling **Lock Placement**
converts it back to the regular attached `\1grd(angle,stops...)` form while
preserving its angle and stops. The rectangle remains cached only for the open
editor session, so toggling it back on can restore the area; it is never stored
in an attached-gradient tag.

The video overlay is available only while a video is loaded. Without one, the
editor can still inspect a placement gradient and edit its angle and stops, but
cannot capture a new rectangle. If the active line's Effect field is `LOCK`, no
placement capture starts. A placement drag also ends safely if the active line
or visual tool changes, the video closes, or Escape/lost mouse capture cancels
the unfinished drag. The current Gradient Editor targets static tags; when the
target gradient is inside `\t(...)`, **Lock Placement** is disabled instead of
inserting a separate static placement tag.
