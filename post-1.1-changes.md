# Post 1.1 Changes

Tracking fixes and behavior changes made after `Toshi-ban V1.1`.

## 2026-02-10

- Fast naming actor commit fix: empty actor input no longer falls back to the last-used MRU actor.
  - File: `src/actor_MRU.cpp`
- Visual tool/panning input fix: video panning now only activates with middle mouse input; left-drag no longer incorrectly pans.
  - File: `src/video_display.cpp`
- `video/play/line` playback stability fixes during undo/redo:
  - Preserve range playback mode when frame restore happens while playing.
  - Avoid unnecessary jump/stop-restart cycle during undo/redo if playback is already continuing.
  - Files: `src/video_controller.cpp`, `src/subs_controller.cpp`
- Dialog Esc-key behavior fixes:
  - Linked files load/unload prompt now supports Esc as cancel/no.
  - Style import resolution mismatch prompt now supports Esc as cancel.
  - Files: `src/project.cpp`, `src/dialog_style_manager.cpp`

