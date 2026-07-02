# Mangetsu Subtitle Renderer

Mangetsu support is implemented as a separate subtitle provider named `Mangetsu`.
It reuses the libassmod-compatible render path, including RGBA rendering and tag
image support, but it is loaded from its own shared library.

The currently supported Mangetsu source is:

https://github.com/amanosatosi/libassmod/tree/mangetsu

## Build And Package

Build the `mangetsu` branch of `libassmod` as a shared library, then package the
output beside `aegisub.exe` or the installed Aegisub binary as:

- Windows: `mangetsu.dll`
- Linux: `libmangetsu.so`
- macOS, if supported: `libmangetsu.dylib`

Configure Aegisub packaging with:

```sh
meson setup build -Dwith_mangetsu=true -Dmangetsu_path=/path/to/mangetsu.dll
```

Use the platform-appropriate path for `mangetsu_path`. The Aegisub build does
not require Mangetsu by default; without this option, Aegisub still builds and
runs, and the Mangetsu provider is simply unavailable unless a matching library
is placed beside the executable by other packaging.

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
