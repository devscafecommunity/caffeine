# Headless UI testing for Doppio (`doppio-headless`)

<div class="doc-meta">
  <span class="doc-badge doc-badge--stable">Stable</span>
  <span class="doc-badge">Doppio</span>
  <span class="doc-badge">Tooling</span>
  <span>Last verified: <time datetime="2026-11-18">2026-11-18</time></span>
  <span>Source: <a href="https://github.com/devscafecommunity/caffeine/blob/main/doppio/crates/ui/src/windows/main/headless/"><code>windows/main/headless/</code></a></span>
</div>

<p class="doc-lede">Drive the real Doppio editor UI without opening a window: click,
drag, type, press shortcuts, read the screen, take screenshots and record GIF or
MP4. One binary, one JSON protocol, no SDK.</p>

<video class="doc-video" autoplay loop muted playsinline width="960"
       poster="images/editor/workspace-scene.png">
  <source src="images/editor/doppio-tour.mp4" type="video/mp4">
  <a href="images/editor/doppio-tour.gif">Watch the interaction (GIF)</a>
</video>

<p class="doc-caption">Recorded by <code>doppio-headless</code> itself: open the Scene
and View menus, then select a hierarchy row. The same script can produce a GIF, MP4
or WebM for a guide or a pull request.</p>

Built so that *any* agent, model, CI job or human can use it the same way; there is
no MCP server, SDK or special client to install.

## Why this exists

| Principle | What it means |
|---|---|
| **Headless** | Nothing opens. Screenshots are rendered off-screen (GPU or CPU). Agents never have to look at a real desktop, which also avoids capturing unrelated windows. |
| **Human-like** | Pointer actions glide in small steps, rest a frame on the target, press, hold, release. Typing is one key at a time. Shortcuts travel the same path as in the live app. Hover, focus, drags and double clicks behave like a person's hand would make them. |
| **Agnostic** | One binary, one JSON protocol, `stdin`/`stdout`/exit codes. Works from a shell, a script, CI, Cursor, Claude Code, Codex, a plain `bash` loop… |
| **Standardised** | One action vocabulary, one target syntax, one result shape, one place that defines them (`reference.rs`), from which validation, JSON Schema and this reference are generated. |
| **Verifiable** | `expect` assertions, JSON `state` snapshots, annotated screenshots and baseline comparison turn "it looks right" into a pass/fail result. |

## Quick start

```bash
# 1. See what is on screen (ids you can target):
./scripts/doppio-headless.sh widgets --size 1440x800

# 2. Run a flow and read the report:
./scripts/doppio-headless.sh run doppio/tests/ui-flows/menus-and-hover.json --format text

# 3. Take an annotated screenshot (every catalogued widget numbered + legend):
./scripts/doppio-headless.sh screenshot --annotate --name home --out /tmp/shots
```

`scripts/doppio-headless.sh` is a thin wrapper around
`cargo run -p doppio-ui --features headless --bin doppio-headless --`. All
arguments are passed through, so everything below also works with the raw
`cargo run` form or with the built binary at
`doppio/target/<profile>/doppio-headless`.

Minimal script:

```json
{
  "name": "open the Scene menu and close it",
  "fixture": "entities",
  "size": [1280, 720],
  "steps": [
    { "action": "click", "target": "id:editor.menu.scene" },
    { "action": "expect", "state": { "/menu": "Scene" }, "exists": "text:Add Entity" },
    { "action": "screenshot", "name": "scene-menu", "annotate": true },
    { "action": "key", "key": "Escape" },
    { "action": "expect", "state": { "/menu": null } }
  ]
}
```

## How it works

The driver owns a real `App` (the same state, `update` and `view` the editor
runs) plus one headless renderer, and repeats the live app's frame loop:

1. the event goes through the widget tree (`UserInterface::update`), with the
   **widget state tree kept between steps** (focus, scroll offsets, canvas
   drags survive from one step to the next);
2. messages published by widgets are applied with `App::update`;
3. events no widget captured also go through the **same mapping the live
   subscriptions use** (`keyboard_event_message`, `global_event_message`: shortcuts,
   fly keys, pointer release, file drop);
4. each step ends with idle frames (a redraw plus the 16 ms `ViewportTick`),
   and the view is rebuilt from the new state before the next event.

### What is and is not simulated

* ✅ Real widgets, layout, hover/press/focus, drags, double clicks, wheel,
  keyboard, text input, shortcuts, file drops, resize, the app's tick.
* ✅ Menus, dialogs, wizard, docking guides, tabs, trees, scrollables, canvases.
* ⚠️ **Fixtures, not an engine.** The session starts from a fixture
  (`entities` or `empty`) with no `libcaffeine-capi`, so the 3D viewport shows
  "Waiting for the engine viewport…". Everything in the editor chrome is real.
  Use the `assets` / `asset_root` / `components` options to give the Assets
  panel, the asset wizard and the Add Behaviour picker something to work on.
* ⚠️ Fonts are the bundled *Fira Sans*, so pixels differ from a desktop
  with other system fonts. Baselines are therefore only valid for headless runs.
* ❌ OS-native dialogs, clipboard from other apps, multiple windows.

## Command line

```text
doppio-headless run <script.json|dir|->...   run scripts, print the report (JSON)
doppio-headless session                      JSON step per line in, result per line out
doppio-headless widgets                      list widgets of the first frame
doppio-headless screenshot                   render the starting fixture
doppio-headless catalog | actions | schema | docs
```

Options (`run`, `session`, `widgets`, `screenshot`):

| Option | Meaning | Default |
|---|---|---|
| `--out <dir>` | Where screenshots/artifacts are written | `doppio-headless-out` |
| `--size <WxH>` | Window size, logical pixels | `1600x900` |
| `--fixture <name>` | `entities`, `empty` or `material` (new material, Generator tab open) | `entities` |
| `--asset-root <dir\|temp>` | Folder the asset wizard writes to (`temp` = scratch dir deleted on exit) | none |
| `--baselines <dir>` | Baseline images for `screenshot.baseline` | none |
| `--update-baselines` | Create/refresh baselines instead of failing | off |
| `--format json\|text` | Report format for `run` | `json` |
| `--name`, `--annotate` | `screenshot` command only | `shot`, off |

A script's own `fixture`, `size`, `assets` and `asset_root` override the
options (which are defaults). Passing a **folder** to `run` runs every
`*.json` in it, each with its own artifact sub-folder.

**Exit codes:** `0` everything passed · `1` a step or assertion failed ·
`2` usage error, unreadable file or invalid script JSON.
**Streams:** reports go to stdout, diagnostics to stderr.

`ICED_TEST_BACKEND=tiny-skia` forces the CPU renderer (CI without a GPU).

### Machine-readable helpers

* `doppio-headless actions` — the action reference as JSON.
* `doppio-headless schema` — JSON Schema (draft-07) of a script; point an
  editor or validator at it.
* `doppio-headless catalog` — every catalogued widget id with kind and
  description (the same catalogue `uikit/id.rs` maintains for the UI).
* `doppio-headless docs` — the Markdown reference at the end of this file.

## Scripts

A script is a JSON object:

```json
{
  "name": "optional title",
  "description": "optional",
  "fixture": "entities",
  "size": [1440, 800],
  "assets": [{ "name": "rock.png", "path": "Textures/rock.png" },
             { "name": "Textures", "path": "Textures", "directory": true }],
  "asset_root": "temp",
  "components": [{ "name": "Mesh Renderer", "fields": ["Mesh", "Material"] }],
  "preset": "Red Brick",
  "steps": [ { "action": "…" } ]
}
```

Every step is an object with an `action` and its parameters, plus the common
`label` (free text echoed in the result) and `continue_on_error`.
Unknown actions or parameters are **rejected with the list of valid ones**
(`unknown parameter "tagret" for "click"; expected: target, …`) so typos never
pass silently. By default a script stops at the first failing step.

### Targets

A target says *what to act on*:

| Form | Example | Notes |
|---|---|---|
| `id:<ui id>` | `id:editor.menu.scene` | **Preferred.** Stable, hierarchical ids from the UI catalogue. |
| `text:<text>` | `text:Add Entity` | Visible label or text-input value; exact match first, then "contains". |
| `point:<x>,<y>` | `point:640,360` | Window coordinates in logical pixels. |
| object | `{ "id": "…" }`, `{ "text": "…" }`, `{ "point": [x, y] }` | Same thing as JSON. |

A bare string is accepted as shorthand (`"assets.wizard"` is an id,
`"Create"` is text) but the prefixes are canonical — use them in committed flows.
`nth` (0-based) picks among several matches and `offset: [dx, dy]` shifts the
click point from the centre of the widget.

Dynamic ids follow the pattern of their catalogue entry:
`hierarchy.list.entity_row.<n>`, `assets.grid.asset_card.<n>`,
`editor.pane.tab.<page>`, `editor.pane.maximize.<Pane(n)>` (the pane ids appear in
`state`)…

### Results

Every step produces:

```json
{
  "index": 3, "action": "click", "label": "open the wizard",
  "ok": true, "elapsed_ms": 45, "frame": 12,
  "data": { "point": [234.25, 548.3], "messages": ["Wizard(Open(Create))"] },
  "error": null, "hint": null
}
```

* `data` is action specific (see the reference). Pointer/keyboard actions list
  the `messages` they caused — the quickest way to see *why* something did or
  did not happen.
* On failure `error` says what went wrong and `hint` suggests the fix
  (`did you mean id:editor.menu.scene?`, `the widget is clipped: maximise the pane`).

`run` wraps the steps in `{ "protocol", "script", "ok", "passed", "failed",
"out_dir", "artifacts", "steps" }`. `--format text` prints the same as a short
human summary.

## Human simulation, precisely

| Action | What is produced |
|---|---|
| `move` | cursor moves from the previous position in `steps` (6) increments, then one idle frame. The first move enters the window from slightly outside the target. |
| `click` | modifiers down → glide → idle frame (the hand rests) → press → release → idle frame. Two *separate* clicks on the same spot are ≥ 330 ms apart so they never count as an accidental double click; `count: 2` / `double_click` produce a real one. |
| `drag` | glide to `from`, press, `steps` (8) pointer samples with an idle frame between each (so canvases/overlays repaint like a real drag), release. `hold: true` keeps the button down; finish with `mouse_up`. |
| `scroll` | glide, then a wheel event in lines. |
| `type` | optional click on `target` to focus it, then **one key at a time** (press/release per character, app updated between keys). |
| `key` | modifiers → key down → key up. Shortcuts (`ctrl+z`, `ctrl+shift+n`, `Escape`, `F5`, `g`, `1`…) reach the app through the same subscription mapping as live. |
| `key_down` / `key_up` | hold keys such as `w` (fly camera) or `shift` (boost). |
| `wait` | idle frames (`frames`) or simulated milliseconds (`ms`, 16 ms/frame). `real_time: true` also sleeps (for background jobs like `caf-pack`). |
| `file_drop` | an OS file-drop event. |

Because the widget state tree persists, a text field stays focused between
steps, scroll offsets stay where you left them, and a canvas keeps its drag
state — exactly what an agent needs for multi-step interactions.

## Discovering the UI (read this, agents)

You never have to guess ids. In order of preference:

1. **`widgets`** — what is on screen *now*:
   `{ "action": "widgets", "prefix": "assets." }` →
   `[{ "id": "assets.toolbar.new", "kind": "container", "bounds": [x,y,w,h], "text": null }, …]`.
   Add `"with_text": true` to also see anonymous labels for `text:` targets.
2. **`find`** — does *this* target exist, where, and is it visible?
3. **`state`** — the app state as JSON (see below). Cheaper than a screenshot.
4. **`screenshot` with `annotate: true`** — numbered boxes and a legend of
   `id — description` for each catalogued widget; look at the picture once,
   then act by id.
5. **`catalog`** — the full list of catalogued ids (not tied to what is open).

If an id is missing, the thing is probably not open: click the tab, open the
menu or wizard first, or `resize` the window if it is clipped.

### The `state` snapshot

| Key | Content |
|---|---|
| `panes[]` | `{ id, pages[], active, maximized }` for every pane (pane ids like `Pane(3)` match `editor.pane.maximize.Pane(3)`) |
| `entities[]`, `selected`, `selected_index` | scene entities (id, name, position, rotation, scale, parent) and selection |
| `menu`, `hierarchy_menu` | open menu name / context-menu row |
| `tab_drag`, `dock_hover` | tab drag in progress (`dragging` is true once the tab left its header) and the docking guide under the cursor |
| `assets` | `directory`, `count`, `selected`, `search`, `view_mode`, `tree_open`, `tree_collapsed` |
| `wizard` | `open`, `tab`, `template`, `name`, `sources`, `packing`, `status { text, ok }` |
| `project`, `material`, `texture_set`, `script`, `viewport` | what is open in each editor |
| `console` | `count` and the `last` 10 entries `{ level, text }` |
| `window`, `pointer`, `frame` | window size, pointer position, frames run |

Use JSON pointers: `{ "action": "state", "path": "/assets/directory" }`.

## Assertions

```json
{ "action": "expect",
  "exists": ["id:assets.wizard", "text:Create"],
  "missing": "id:assets.toolbar.nope",
  "visible": "id:editor.pane.tab.console",
  "shows": "Waiting for the engine viewport…",
  "text": { "target": "id:inspector.entity.name", "equals": "Entity 1" },
  "state": { "/wizard/open": true, "/assets/tree_collapsed": { "contains": "Textures" } },
  "count": { "prefix": "assets.grid.asset_card.", "at_least": 3 } }
```

Every given field must hold. Put a `label` on assertions so failures read well
in the report.

## Screenshots

```json
{ "action": "screenshot", "name": "wizard", "target": "id:assets.wizard",
  "annotate": ["id:assets.wizard.create.name", "id:assets.wizard.create.confirm"],
  "scale": 2, "baseline": "wizard-create" }
```

* Files are `<out>/<NNN>-<name>.png` (NNN counts up so nothing is overwritten);
  the result gives `path`, `width`, `height` and a content `hash`.
* `target` crops to a widget (+ `padding`); `scale` raises the pixel density.
* `annotate: true` numbers every *catalogued* widget (one box per id, no
  pane-sized frames) and appends the legend `n. id — description`; or pass a
  list of targets to label exactly those.
* **Baselines**: with `--baselines <dir>`, `baseline` compares against
  `<dir>/<name>.png`. First run (or `--update-baselines`) writes it; later
  mismatches beyond `tolerance` (fraction of pixels, default 0.0005) fail the
  step and save a `*.diff.png` with differing pixels in red.

## Flows (regression tests)

Flows are scripts committed under `doppio/tests/ui-flows/*.json`. The Rust test
`every_shipped_flow_passes` runs all of them on every `cargo test`, and
`scripts/check-rust-ui.sh` does too. To add one: copy a flow, change the steps,
run it with `--format text`, commit it. A good flow

* names the behaviour in `name`/`description` (and the bug it guards, if any);
* starts from a fixture and only uses ids/texts a user could see;
* asserts with `expect` after each meaningful action (not only at the end);
* keeps screenshots as *artifacts* (not baselines) unless the visuals are the point.

## Recipes for agents

**Verify a UI change you just made**

```bash
./scripts/doppio-headless.sh run doppio/tests/ui-flows --format text      # all flows
./scripts/doppio-headless.sh screenshot --annotate --name after --out /tmp/after
```

Then read `/tmp/after/001-after.png`.

**Reproduce a bug as a user would**: write the clicks as a script, run it with
`--format text`, and read the `messages` of the failing step.

**Interactive exploration without any framework** — a persistent session over
pipes (one JSON step per line, one JSON result per line):

```bash
mkfifo /tmp/dh.in
./scripts/doppio-headless.sh session --out /tmp/dh < /tmp/dh.in &
exec 3>/tmp/dh.in
echo '{"action":"widgets","prefix":"editor.menu"}'        >&3
echo '{"action":"click","target":"id:editor.menu.scene"}' >&3
echo '{"action":"screenshot","name":"menu"}'              >&3
echo '{"action":"quit"}'                                  >&3
```

```python
import json, subprocess
p = subprocess.Popen(["scripts/doppio-headless.sh", "session"], text=True,
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE)
print(p.stdout.readline())                      # {"event":"ready",...}
def step(**s):
    p.stdin.write(json.dumps(s) + "\n"); p.stdin.flush()
    return json.loads(p.stdout.readline())
assert step(action="click", target="id:editor.menu.scene")["ok"]
print(step(action="state", path="/menu")["data"])   # "Scene"
```

## Documentation captures

The docs use this driver instead of desktop screenshots. Capture flows live in
`doppio/docs/flows/capture-*.json`; the shell wrappers only strip the numeric prefix
so image names stay stable:

```bash
./scripts/capture-editor-docs.sh    # workspace, panes, menus, generators
./scripts/capture-editor-tour.sh    # short MP4/GIF tour (ffmpeg for GIF)
./scripts/capture-material-docs.sh
./scripts/capture-nature-docs.sh
./scripts/capture-terrain-docs.sh
./scripts/capture-texture-docs.sh
./scripts/capture-water-docs.sh
./scripts/capture-gizmo-docs.sh
```

A capture flow is a normal flow plus `screenshot` steps:

```json
{ "action": "screenshot", "name": "pane-inspector",
  "target": "id:editor.pane.inspector", "padding": 4 }
```

Use `target` to crop to a pane, `padding` to breathe, `scale: 2` for retina-like
detail and `annotate` when the point is to teach ids. Keep captures as artifacts,
not baselines, unless the visuals are the regression.

For motion, record the flow itself:

```json
{ "steps": [
  { "action": "record_start", "name": "tour", "format": "mp4", "fps": 12, "width": 960 },
  { "action": "click", "target": "id:editor.menu.scene" },
  { "action": "wait", "ms": 700, "real_time": true },
  { "action": "key", "key": "Escape" },
  { "action": "record_stop" }
] }
```

`format` accepts `gif`, `mp4` and `webm`. A GIF is the most portable fallback;
MP4 is smaller and sharper for modern pages. Use `width` to keep the file small and
`keep_frames` only when you need the intermediate PNGs.

The UI reference is generated separately:
`./scripts/generate-doppio-ui-docs.sh --publish` captures every catalogued widget
from `ui-captions.json`. Never hand-edit `editor/ui-reference.md`.

## Extending the driver

* **New action**: add its `ActionDoc` in `headless/reference.rs` (parameters,
  returns, example), handle it in `Driver::execute`, add a test in
  `headless/tests.rs`, then refresh this file's reference
  (`./scripts/doppio-headless.sh docs`, paste between the markers — a test
  fails if they drift apart).
* **New state key**: add it to `headless/state.rs` and the table above.
* **New widget**: give it a `UiId` + catalogue entry (`uikit/id.rs`) as the
  project rules require — that automatically makes it targetable, listed by
  `widgets` and annotatable.
* Keep the protocol additive; bump `PROTOCOL_VERSION` on breaking changes.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `could not create a headless renderer` | `ICED_TEST_BACKEND=tiny-skia` |
| `no widget matches id:…` | read the `hint`; run `widgets`; open the tab/menu/dialog that owns it |
| `the widget exists but is clipped` | `resize`, maximise the pane, or scroll its container |
| Click did nothing | read `data.messages`; was a modal/overlay above it? Is the target the container instead of the button text? Try `text:` |
| Two clicks became a double click | use one `click` per intent; the driver already spaces separate clicks |
| Baseline mismatch only on another machine | fonts/AA differ; raise `tolerance` or regenerate baselines there |
| Typing lands nowhere | pass `target` to `type` so the field is clicked (focused) first |

## Reference

### Recording GIFs and videos (ffmpeg)

`record_start` / `record_stop` capture the window (or one widget) while a script runs and let **ffmpeg**
encode a **GIF, MP4 or WebM**. A frame is taken after every step that changes something (so a click is
visible) and every few frames during a `wait`; with `real_time: true` the wait plays at normal speed, so
`{ "action": "wait", "ms": 2000, "real_time": true }` is about two seconds of video. `fps` is both the capture
and the playback rate. ffmpeg must be on the `PATH` (or set `FFMPEG` to its path); without it the step fails
with a hint and the frames are kept. A recording left open at the end of a script is finished automatically.

```json
{ "steps": [
  { "action": "record_start", "name": "orbit", "format": "mp4", "fps": 30, "target": "id:editor.pane.terrain_generator" },
  { "action": "click", "target": "id:terraingen.preset" },
  { "action": "wait", "ms": 2000, "real_time": true },
  { "action": "record_stop" }
] }
```

The result of `record_stop` reports `path`, `frames`, `seconds` and `bytes`. Use `width` for a smaller file,
`scale` for a sharper capture and `keep_frames` to keep the PNGs.

The sections below are generated from `headless/reference.rs`.

<!-- BEGIN GENERATED REFERENCE -->

### Pointer

#### `move`

Moves the pointer to a target in several small steps (hover).

| Parameter | Type | Required | Description |
|---|---|---|---|
| `target` | `target` | yes | What to act on: `id:<ui id>`, `text:<visible text>` or `point:<x>,<y>` (see Targets). |
| `nth` | `int` | no | 0-based index when several widgets match the target (default 0). |
| `offset` | `[x, y]` | no | Logical pixels added to the target's centre, e.g. to hit a corner. |
| `steps` | `int` | no | Intermediate pointer positions emitted while moving, like a real hand (default 6). |

Returns: `{ "point": [x, y] }`

```json
{ "action": "move", "target": "id:editor.menu.scene" }
```
#### `click`

Moves to the target, presses and releases a mouse button.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `target` | `target` | yes | What to act on: `id:<ui id>`, `text:<visible text>` or `point:<x>,<y>` (see Targets). |
| `nth` | `int` | no | 0-based index when several widgets match the target (default 0). |
| `offset` | `[x, y]` | no | Logical pixels added to the target's centre, e.g. to hit a corner. |
| `button` | `"left" \| "right" \| "middle"` | no | Mouse button (default left). |
| `modifiers` | `["ctrl" \| "shift" \| "alt" \| "cmd"]` | no | Modifier keys held during the action. |
| `count` | `int` | no | 1 = click, 2 = double click, 3 = triple (default 1). |
| `steps` | `int` | no | Intermediate pointer positions emitted while moving, like a real hand (default 6). |

Returns: `{ "point": [x, y], "messages": ["…"] }`

```json
{ "action": "click", "target": "id:assets.toolbar.new" }
```
#### `double_click`

Shorthand for `click` with `count: 2`.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `target` | `target` | yes | What to act on: `id:<ui id>`, `text:<visible text>` or `point:<x>,<y>` (see Targets). |
| `nth` | `int` | no | 0-based index when several widgets match the target (default 0). |
| `offset` | `[x, y]` | no | Logical pixels added to the target's centre, e.g. to hit a corner. |
| `button` | `"left" \| "right" \| "middle"` | no | Mouse button (default left). |
| `modifiers` | `["ctrl" \| "shift" \| "alt" \| "cmd"]` | no | Modifier keys held during the action. |
| `steps` | `int` | no | Intermediate pointer positions emitted while moving, like a real hand (default 6). |

Returns: `as `click``

```json
{ "action": "double_click", "target": "text:Textures" }
```
#### `right_click`

Shorthand for `click` with `button: "right"` (context menus).

| Parameter | Type | Required | Description |
|---|---|---|---|
| `target` | `target` | yes | What to act on: `id:<ui id>`, `text:<visible text>` or `point:<x>,<y>` (see Targets). |
| `nth` | `int` | no | 0-based index when several widgets match the target (default 0). |
| `offset` | `[x, y]` | no | Logical pixels added to the target's centre, e.g. to hit a corner. |
| `modifiers` | `["ctrl" \| "shift" \| "alt" \| "cmd"]` | no | Modifier keys held during the action. |
| `steps` | `int` | no | Intermediate pointer positions emitted while moving, like a real hand (default 6). |

Returns: `as `click``

```json
{ "action": "right_click", "target": "id:hierarchy.list.entity_row.0" }
```
#### `drag`

Presses on `from`, moves in small steps and releases on `to` (or by an offset).

| Parameter | Type | Required | Description |
|---|---|---|---|
| `from` | `target` | yes | Where the drag starts. |
| `to` | `target` | no | Where the drag ends. One of `to` / `by` is required. |
| `by` | `[dx, dy]` | no | Relative drag distance in logical pixels. |
| `button` | `"left" \| "right" \| "middle"` | no | Mouse button (default left). |
| `modifiers` | `["ctrl" \| "shift" \| "alt" \| "cmd"]` | no | Modifier keys held during the action. |
| `steps` | `int` | no | Intermediate pointer positions emitted while moving, like a real hand (default 6). |
| `hold` | `bool` | no | Do not release at the end (finish later with `mouse_up`). Default false. |

Returns: `{ "from": [x, y], "to": [x, y], "messages": ["…"] }`

```json
{ "action": "drag", "from": "id:editor.pane.tab.console", "to": "point:900,300" }
```
#### `scroll`

Moves to the target and turns the mouse wheel.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `target` | `target` | no | Where to act; omit to act at the current pointer position. |
| `dy` | `number` | no | Wheel lines vertically; positive scrolls up/zooms in (default 0). |
| `dx` | `number` | no | Wheel lines horizontally (default 0). |

Returns: `{ "point": [x, y] }`

```json
{ "action": "scroll", "target": "id:editor.viewport.surface", "dy": 3 }
```
#### `mouse_down`

Low-level: presses a mouse button at the current pointer position.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `button` | `"left" \| "right" \| "middle"` | no | Mouse button (default left). |

Returns: `{}`

```json
{ "action": "mouse_down" }
```
#### `mouse_up`

Low-level: releases a mouse button at the current pointer position.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `button` | `"left" \| "right" \| "middle"` | no | Mouse button (default left). |

Returns: `{}`

```json
{ "action": "mouse_up" }
```

### Keyboard

#### `type`

Types text one character at a time. With `target`, clicks it first to focus it.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `text` | `string` | yes | The characters to type. |
| `target` | `target` | no | Where to act; omit to act at the current pointer position. |
| `clear` | `bool` | no | Select all (ctrl+a) and delete before typing. |

Returns: `{ "messages": ["…"] }`

```json
{ "action": "type", "target": "id:assets.wizard.create.name", "text": "Props" }
```
#### `key`

Taps a key or chord such as `Enter`, `Escape`, `F5`, `ctrl+z`, `ctrl+shift+n`.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | `string` | yes | Key name (enter, escape, tab, space, backspace, delete, up/down/left/right, home, end, pageup, pagedown, f1-f12, or one character), optionally prefixed by `ctrl+`, `shift+`, `alt+`, `cmd+`. |

Returns: `{ "messages": ["…"] }`

```json
{ "action": "key", "key": "ctrl+z" }
```
#### `key_down`

Presses a key and keeps it held (fly camera keys, shift boost). Release with `key_up`.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | `string` | yes | Same syntax as `key`. |

Returns: `{}`

```json
{ "action": "key_down", "key": "w" }
```
#### `key_up`

Releases a key held by `key_down`.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | `string` | yes | Same syntax as `key`. |

Returns: `{}`

```json
{ "action": "key_up", "key": "w" }
```

### Time

#### `wait`

Lets the app run: delivers redraws and the 16 ms viewport tick.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `frames` | `int` | no | Number of frames to run (default 1). |
| `ms` | `int` | no | Alternative to `frames`: milliseconds of simulated time (16 ms per frame). |
| `real_time` | `bool` | no | Also sleep for that long (for background jobs such as caf-pack). Default false. |

Returns: `{ "frames": n }`

```json
{ "action": "wait", "ms": 200 }
```
#### `resize`

Resizes the virtual window (logical pixels).

| Parameter | Type | Required | Description |
|---|---|---|---|
| `size` | `[w, h]` | yes | New window size. |

Returns: `{ "size": [w, h] }`

```json
{ "action": "resize", "size": [1280, 720] }
```
#### `file_drop`

Drops a file from the OS onto the window.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `path` | `string` | yes | Absolute path of the dropped file or folder. |

Returns: `{ "messages": ["…"] }`

```json
{ "action": "file_drop", "path": "/tmp/rock.png" }
```

### Query

#### `find`

Looks a target up without touching it.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `target` | `target` | yes | What to act on: `id:<ui id>`, `text:<visible text>` or `point:<x>,<y>` (see Targets). |
| `nth` | `int` | no | 0-based index when several widgets match the target (default 0). |

Returns: `{ "found": bool, "id": …, "kind": …, "bounds": [x,y,w,h], "visible_bounds": …, "text": …, "matches": n }`

```json
{ "action": "find", "target": "id:editor.pane.viewport" }
```
#### `widgets`

Lists the widgets of the current frame (discovery: what can I click?).

| Parameter | Type | Required | Description |
|---|---|---|---|
| `prefix` | `string` | no | Only ids that start with this (e.g. `assets.`). |
| `kind` | `string` | no | Only this kind: container, text, text_input, focusable, scrollable, custom. |
| `visible_only` | `bool` | no | Skip widgets that are clipped away (default true). |
| `with_text` | `bool` | no | Include anonymous text widgets so you can target `text:` (default false). |

Returns: `{ "count": n, "widgets": [{ "id", "kind", "bounds", "text" }] }`

```json
{ "action": "widgets", "prefix": "assets." }
```
#### `text`

Reads the text of a text widget or the value of a text input.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `target` | `target` | yes | What to act on: `id:<ui id>`, `text:<visible text>` or `point:<x>,<y>` (see Targets). |
| `nth` | `int` | no | 0-based index when several widgets match the target (default 0). |

Returns: `{ "text": "…" }`

```json
{ "action": "text", "target": "id:inspector.entity.name" }
```
#### `state`

Returns the app state as JSON (panes, selection, menus, wizard, console…).

| Parameter | Type | Required | Description |
|---|---|---|---|
| `path` | `string` | no | JSON pointer into the state, e.g. `/panes/0/pages`. Omit for everything. |

Returns: `the state (or the selected part)`

```json
{ "action": "state", "path": "/menu" }
```
#### `log`

Returns the last console entries.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `last` | `int` | no | How many entries (default 10). |

Returns: `{ "entries": [{ "level", "text" }] }`

```json
{ "action": "log", "last": 5 }
```

### Capture

#### `screenshot`

Renders the window (or one widget) to a PNG, optionally annotated or compared with a baseline.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | `string` | no | File stem (default `shot`). Files are `<out>/<NNN>-<name>.png`. |
| `target` | `target` | no | Crop to this widget; omit for the whole window. |
| `padding` | `number` | no | Logical pixels added around a cropped target (default 8). |
| `annotate` | `bool \| [target]` | no | Draw numbered boxes with a legend. `true` = every visible widget that has a catalogued id; or a list of targets. |
| `scale` | `number` | no | Pixel density (default 1; use 2 for retina-like detail). |
| `baseline` | `string` | no | Compare with `<baselines>/<baseline>.png`; a mismatch fails the step. Create/refresh with `--update-baselines`. |
| `tolerance` | `number` | no | Allowed fraction of differing pixels (default 0.0005). |

Returns: `{ "path", "width", "height", "hash", "baseline": { "status", "diff_ratio" } }`

```json
{ "action": "screenshot", "name": "assets-wizard", "target": "id:assets.wizard", "annotate": true }
```
#### `record_start`

Starts recording the window (or one widget). Frames are taken after every step that changes something and every few frames during `wait`; `record_stop` turns them into a GIF, MP4 or WebM with ffmpeg.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | `string` | no | File stem of the result (default `recording`): `<out>/<name>.<format>`. |
| `format` | `string` | no | `gif` (default), `mp4` or `webm`. |
| `fps` | `int` | no | Capture and playback rate, 1-60 (default 15). A `wait` of 2000 ms with `real_time` is about two seconds of video. |
| `target` | `target` | no | Record only this widget; omit for the whole window. |
| `padding` | `number` | no | Logical pixels around a target (default 8). |
| `scale` | `number` | no | Capture pixel density, 0.25-3 (default 1). |
| `width` | `int` | no | Output width in pixels (height keeps the aspect); omit to keep the captured size. |
| `keep_frames` | `bool` | no | Keep the PNG frames (their folder is returned) instead of deleting them. |

Returns: `{ "recording": true, "fps" }`

```json
{ "action": "record_start", "name": "orbit", "format": "mp4", "fps": 30, "target": "id:editor.pane.viewport" }
```
#### `record_stop`

Stops the recording and encodes it with ffmpeg (the FFMPEG environment variable overrides the program path). A recording still open when the script ends is finished automatically.

Returns: `{ "path", "format", "frames", "fps", "seconds", "bytes" }`

```json
{ "action": "record_stop" }
```

### Assert

#### `expect`

Asserts conditions on the current frame/state; every given field must hold.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `exists` | `target \| [target]` | no | These targets are on screen. |
| `missing` | `target \| [target]` | no | These targets are not on screen. |
| `visible` | `target \| [target]` | no | These targets exist and are not clipped away. |
| `shows` | `string \| [string]` | no | These exact texts are on screen. |
| `text` | `{ target, equals?, contains? }` | no | Text of a widget equals / contains a string. |
| `state` | `{ "/json/pointer": value }` | no | Values in the `state` JSON (deep equality). Use `{ "contains": … }` for arrays/strings. |
| `count` | `{ prefix, equals?, at_least? }` | no | Number of visible widgets whose id starts with `prefix`. |

Returns: `{ "checked": n }`

```json
{ "action": "expect", "exists": "id:assets.wizard", "state": { "/wizard/open": true } }
```

### Session

#### `reset`

Starts over with a fresh app (new fixture, window size and assets).

| Parameter | Type | Required | Description |
|---|---|---|---|
| `fixture` | `string` | no | `empty`, `entities`, `material`, `texture`, `scene3d` (an engine-backed scene with lights, a camera and meshes; needs libcaffeine-capi) `nature` (the same, filled with generated rocks, trees and plants) `water` (the Water Generator tab in front) or `terrain` (the Terrain Generator tab in front). Default `entities`. |
| `size` | `[w, h]` | no | Window size. |
| `assets` | `[{ name, path, kind, directory? }]` | no | Asset list shown in the Assets panel. |
| `asset_root` | `string` | no | Folder the asset wizard writes to; `temp` creates a scratch directory. |
| `preset` | `string` | no | Library preset applied to the Material Generator (fixture `material`, e.g. `Red Brick`) to the Texture Generator (fixture `texture`, e.g. `Torch Flame`) to the Nature Generator (fixture `nature`, e.g. `Pine`) to the Water Generator (fixture `water`, e.g. `Storm`) or to the Terrain Generator (fixture `terrain`, e.g. `Badlands`). |
| `components` | `[{ name, fields? }]` | no | Engine component types the fixture pretends to have (for Add Behaviour). |

Returns: `{ "asset_root": … }`

```json
{ "action": "reset", "fixture": "entities", "asset_root": "temp" }
```

<!-- END GENERATED REFERENCE -->
