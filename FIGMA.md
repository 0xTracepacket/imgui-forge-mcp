# Figma ↔ ImGui bridge

Design the UI in Figma and see it live in ImGui, or edit the spec and see it update
in Figma — both directions.

```
   ImGui (ui_spec.json) ──file watch──▶ bridge ──WebSocket──▶ Figma plugin ──▶ canvas
   ImGui (live preview) ◀──hot reload── bridge ◀──WebSocket── Figma plugin ◀── your edits
```

The bridge (`figma/bridge.mjs`) owns the conversion (`figma/convert.mjs`), so the
Figma plugin stays thin: it paints a node model onto the canvas and reads it back.
Round-trip is lossless for tool-created nodes (each carries `imgui` plugin-data);
hand-drawn Figma nodes import best-effort (text→text, frames→groups).

## Setup

1. Install deps: `cd figma && npm install`
2. Start the bridge (keep it running): `node figma/bridge.mjs`
   Listens on `ws://127.0.0.1:8377`, watches `spec/ui_spec.json`.
   Override with env `IMGUI_SPEC_PATH`, `IMGUI_BRIDGE_PORT`.
3. Load the plugin in the Figma desktop app:
   **Menu → Plugins → Development → Import plugin from manifest…** → pick
   `figma/plugin/manifest.json`, then run **imgui-tool — sync**.
4. (Optional) run the preview: `build\imgui_tool.exe spec\ui_spec.json`.

## Use

In the plugin panel:
- **⬇ Pull from ImGui** — paints the current `ui_spec.json` onto the canvas. Also
  happens on connect and whenever the spec changes.
- **⬆ Push to ImGui** — serializes the selected frame (or the exported root) back
  into `ui_spec.json`; the preview hot-reloads.
- **Auto-push** — debounced push on every canvas edit.

## What maps

| ImGui | Figma |
|-------|-------|
| `window` | frame with title, fills, rounded |
| `button` / `checkbox` | styled frame + label / box + label |
| `slider_float` / `slider_int` | label + track with fill |
| `input_text` / `input_int` / `input_float` | label + boxed value |
| `combo` / `listbox` | label + boxed value + caret |
| `color_edit*` / `color_picker*` | label + swatch |
| `group` / `child` / `collapsing_header` | panel frame |
| `tab_bar` / `tab_item` | nested frames carrying `imgui` role metadata |

Colors come from `figma/palette.mjs`. The `imgui` plugin-data on each node is what
makes the Figma→ImGui import exact.

## Troubleshooting

- **"Invalid value for allowedDomains 'ws://…'"** — Figma rejects the `ws://` scheme;
  the manifest uses `["*"]` for local dev. Re-import after any manifest change.
- **Plugin dot stays red** — the bridge isn't running. Start `node figma/bridge.mjs`.
- **Nothing paints on Pull** — run `cd figma && npm install`, and ensure
  `spec/ui_spec.json` is non-empty.

## Verify without Figma

- Converter round-trip: `node figma/test_roundtrip.mjs`
- Live bridge loop: start the bridge, then `node figma/test_bridge.mjs`
