# aegisub toshi-ban v1.0

> *vibe-coded out of necessity*

This is my personal Aegisub fork. I made it to fix the issues I hit while doing real subtitle work.
I didn’t plan to become a dev. I just wanted the tool to stop getting in my way.

Development time: **~2 months**  
Made for: **users first**

---

## Install

### Portable
1. Download the portable zip from Releases
2. Extract anywhere
3. Run `aegisub.exe`

### Installer
1. Download the installer from Releases
2. Install like a normal Windows app

> If something works in portable but not installer, please report it. I test both, but mistakes can happen.
>  **Note:** The **wx master variant** of releases includes **experimental dark mode**. From my tests, it’s pretty stable, very small parts may still briefly appear in light mode.

---

## Why this exists

I haven’t seen any fork that fixes the problems I personally hit every day, so I fixed them myself.
I used AI tools (Codex) to help implement and iterate faster.

This project is open source for transparency, and tested by actually using it.

---

## Notable features & changes

### Subtitle editing
- **Show `\N` as a real line break** in the grid *(optional)*
<img width="610" height="137" alt="image" src="https://github.com/user-attachments/assets/06dbeba9-0d97-4f71-b830-c787f3bc3657" />

- **K-timing cuts no longer r

https://github.com/user-attachments/assets/3a9f5606-9d49-4984-84d1-de58a4e3c1d1

eset existing timing**
  
- **Japanese brackets support**: Easier insertion and editing without changing keyboard layouts
- <img width="390" height="366" alt="image" src="https://github.com/user-attachments/assets/165b7117-bd83-4f9c-92f5-a4c23c784df7" />

- **Join next**: merge current line with the next line for timing. Texts from next line will go to the original edit box. 
- **Join last**: merge previous line into the edit box for both timing and texts.
* **Selection color change support**: Change color of selected text parts easily


https://github.com/user-attachments/assets/680a29a6-0b1d-4a98-990d-408ddb641011


* **Added gradient tag GUI**: Easier editing for VSFilterMod gradient tags
  

https://github.com/user-attachments/assets/85be641b-4d56-47c1-9c09-e432611f32b9


* **Add color tags inside `\t`**: Works inside transformations

  > *A bit clunky in some cases, but fully functional*
- **Faster Actor naming workflow**
- * Auto-complete on actor field
* **Fast Naming Mode**
  * **Carry over**: name from current line is automatically set in next line when pressing Enter
  * **Recent names list**: stores up to 10 names for fast selection
  * **Nanashi mode (optional)**: Ctrl works as Enter, Alt navigates to previous line
  - Note: **CJK / complex IMEs may not be fully supported yet**

If you want an Actor-name workflow without writing scripts, check out **Irodori**: *(link)*

---

### Styles and other improvements

* **Importing styles improvements**:

  * Warns when `.ass` file being imported has a different resolution
  * Option to **Replace All** when multiple style names conflict
  * Manual checking is still available

* **Relative time click**: click on line time to copy/insert into edit box

* **LOCK effect field**: type `LOCK` (all caps) to disable visual tools for that line

* **Force Zoom**: checkbox to override zoom data in `.ass` and use your default zoom when loading associated video
---

**For returning users / old configs**

* Suggested new shortcut: **Alt + C → Edit / Line / Comment**

  > After using it, focus returns to the edit box (or wherever your cursor was)
* New variant: **Edit / Line / Comment / Invert**

  > Inverts comment state for selected lines instead of toggling all at once

> ⚠️ Note: If you already have custom shortcuts, this is **optional**. Your previous config won’t be automatically overwritten.

---

### Utilities / Scripts

- **Compatibility fix for Aegisub 3.2.2 KFX templates**
* **[Dependency control configure script](https://github.com/garret1317/aegisub-scripts/blob/master/garret.depctrl_config.lua)** (from [garret1317](https://github.com/garret1317)様)

  > Addresses the lack of configureability in the default dependency control system

---
### Playback & media
- **Playback speed control (up to 4×)** 
- **Multi-audio track info**: codec, channels, language, track name
<img width="444" height="240" alt="image" src="https://github.com/user-attachments/assets/75173d12-58c0-4d70-ab84-3360922ae665" />

---

### UI
- **New icons** for better visibility in experimental dark mode

---

## Themes

* Includes theme collections from [sgt0](https://github.com/sgt0/aegisub-themes) and unofficial dark mode from me.
---

## Theme credits

Themes included in this release, with original authorship preserved.

* **Ayu (Light / Dark / Mirage)** — dempfi
  [https://github.com/dempfi/ayu](https://github.com/dempfi/ayu)

* **Catppuccin (Latte / Mocha)** — Catppuccin Project
  [https://github.com/catppuccin/catppuccin](https://github.com/catppuccin/catppuccin)

* **Dracula** — The Dracula Theme Team
  [https://github.com/dracula/dracula-theme](https://github.com/dracula/dracula-theme)

* **Solarized (Dark / Light)** — Ethan Schoonover
  [https://ethanschoonover.com/solarized](https://ethanschoonover.com/solarized)

* **Zenburn** — Jani Nurminen
  [https://github.com/jnurmine/Zenburn](https://github.com/jnurmine/Zenburn)

* **Seti** — Jesse Weed
  [https://github.com/jesseweed/seti-ui](https://github.com/jesseweed/seti-ui)

* **Sakura** — Misterio77
  [https://github.com/Misterio77/base16-sakura-scheme](https://github.com/Misterio77/base16-sakura-scheme)

* **Mountain** — gnsfujiwara
  [https://github.com/tinted-theming/base16-schemes/blob/main/mountain.yaml](https://github.com/tinted-theming/base16-schemes/blob/main/mountain.yaml)

---

**Thanks to sgt0** for collecting and maintaining a public Aegisub theme bundle, which made including these themes possible:
[https://github.com/sgt0/aegisub-themes](https://github.com/sgt0/aegisub-themes)

> Theme authorship belongs to their respective creators.
> sgt0 is credited for assembling and distributing the theme collection.

---

 ## Built on top of improvements from arch1t3ct’s fork, including:

 * Video panning
 * Line folding
 * Perspective tool
 * Lua API additions
 * Stereo audio


---

## Bug reports & feedback

- GitHub Issues: [Link](https://github.com/amanosatosi/JibunSenyou-Aegisub/issues/new/choose)
- Telegram: ([link](https://t.me/waipuro))

Good bug reports help a lot: include steps to reproduce + a screenshot if possible.

---

## Future updates

Only **two more updates** are planned:
1. Bug-fix release (if bugs are reported)
2. wxWidgets 3.3.2 update (better dark mode stability)

No feature expansion beyond that.

---

## Platform support
- **Windows**: supported
- **Linux**: supported
- **macOS**: not supported (no hardware to test reliably)

---

## Who this is for

This fork is for people who actually use Aegisub.  
The priority is simple: **works well, feels good, wastes less time**.
