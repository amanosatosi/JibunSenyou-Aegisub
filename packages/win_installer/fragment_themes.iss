; Install bundled theme preset JSONs for both installer and portable builds.
; These land in {app}\share\aegisub\themes, matching the data path used at runtime.

[Files]
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\aegisub_default.json;       Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\ayu_dark.json;              Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\ayu_light.json;             Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\ayu_mirage.json;            Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\catppuccin_latte.json;      Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\catppuccin_mocha.json;      Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\dark_mode_unofficial.json;  Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\dracula.json;               Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\mountain.json;              Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\poppo.json;                 Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\sakura.json;                Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\seti.json;                  Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\solarized_light.json;       Flags: ignoreversion; Components: main
DestDir: {app}\share\aegisub\themes; Source: {#SOURCE_ROOT}\src\themes\zenburn.json;               Flags: ignoreversion; Components: main
