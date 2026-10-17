# Manual acceptance

These checks need a running, installed LMMS. They were not executed in this implementation pass. Do not mark them done from the offscreen or translation-unit results.

## Display

- [ ] 100%, 125%, 150%, 156.25%, 175%, and 200% on the main window, mixer, piano roll, and browser
- [ ] Move a window between two monitors with different scale factors, 20 times
- [ ] Dock and undock the piano roll and an instrument window
- [ ] Theme change and a larger application font, English and German
- [ ] Keyboard-only path: play/stop, focus a knob, open and cancel a dialog
- [ ] Screen-reader spot check of transport, a knob, track selection, and a dialog

## Audio

- [ ] 30 minutes of playback of a normal project at 48 kHz / 256 frames with the browser and meters open
- [ ] Change buffer size and confirm the oscilloscope follows the new period
- [ ] Unplug the audio device and confirm a visible error and a way to pick another device
- [ ] Listen to an internal instrument before and after the worker and thumbnail changes

## Plugins and MCP

- [ ] Open a remote plugin, hide its UI, and close the project
- [ ] Confirm a non-responding plugin helper does not freeze the rest of the song and that quit finishes
- [ ] MCP: read, preview, apply, undo, redo, save, reload
- [ ] MCP off, and `--no-mcp`, with no listening socket
- [ ] Render a multi-minute WAV and fetch it in chunks

## Session

- [ ] Two LMMS processes at once: quit one and confirm the other recovery file remains
- [ ] Autosave failure (read-only recovery directory) shows a status message and does not clear the modified flag
- [ ] Missing project with a `.bak` offers the backup and does not discard the current song on cancel
