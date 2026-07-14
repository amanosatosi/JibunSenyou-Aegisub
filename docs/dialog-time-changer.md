# Dialog Time Changer

Dialog Time Changer is an audio display timing edit mode. Hold Shift and drag with the left mouse button over the audio display to show the yellow timing-change overlay and apply one of the following edits on mouse release.

- Drag wholly inside the active line to remove that time range. A range at the start trims the start, a range at the end trims the end, and a middle range splits the line into two duplicate lines with only timings changed.
- Drag from the active line into the direct next line to move the shared boundary later to the drag end.
- Drag from the direct next line back into the active line to move the shared boundary earlier to the drag end.

The edit refuses results shorter than 100 ms and only modifies the active line plus, for boundary edits, its direct next line. Playback and selection updates are not undoable; the subtitle timing edit itself is one undo action.

## Manual Checks

- Missing or too-short result: drag so a resulting line would be under 100 ms; no timing edit should be committed.
- Start trim: Shift+left drag from the active line start toward the middle; the active line start moves to the drag end and the line auto-previews when enabled.
- End trim: Shift+left drag from the middle to the active line end; the active line end moves to the drag start and the line auto-previews when enabled.
- Split: Shift+left drag across the middle of the active line; two duplicate lines remain with only timings changed, and playback covers the first start through the second end.
- Boundary later: Shift+left drag from the active line into the direct next line; the active end and next start both become the drag end.
- Boundary earlier: Shift+left drag from the direct next line back into the active line; the active end and next start both become the drag end.
- Crossing more than the direct next line should not modify subtitles.
- Join Next with one active line should leave the joined line selected/focused, set the audio timing selection to the joined timing, and preview it once when enabled.
