// Loaded by capture_doppio_ui.py through KWin's scripting D-Bus API.
const windows = workspace.windowList();
for (let i = 0; i < windows.length; i++) {
    const caption = windows[i].caption || "";
    if (caption.indexOf("Doppio") !== -1) {
        workspace.activeWindow = windows[i];
        break;
    }
}
