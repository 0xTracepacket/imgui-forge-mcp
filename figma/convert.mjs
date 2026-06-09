// Convert between ui_spec.json and a Figma node model.
//   specToFigma(spec)  -> a node tree the plugin paints onto the canvas
//   figmaToSpec(model) -> ui_spec.json, rebuilt from the `imgui` data each node carries
// Round-trip is lossless for nodes the tool created; hand-drawn Figma nodes
// import best-effort (text -> text, frames -> groups).
import { paint } from "./palette.mjs";

const T = (chars, size, token) => ({ type: "TEXT", characters: String(chars ?? ""), fontSize: size, textPaint: paint(token), fills: [] });
const F = (name, opts = {}, children = []) => ({ type: "FRAME", name, fills: [], children, ...opts });
const deco = (n) => { n.imgui = { role: "decoration" }; return n; };

// ---------------- spec -> figma ----------------
export function specToFigma(spec) {
  const windows = Array.isArray(spec?.windows) ? spec.windows : [];
  return F("imgui-export", { layoutMode: "VERTICAL", itemSpacing: 32, imgui: { role: "root" } },
    windows.map(windowToFig).filter(Boolean));
}

function windowToFig(win) {
  if (!win || typeof win !== "object") return null;
  const p = win.props || {};
  return F(`window: ${p.title || "Window"}`, {
    w: (p.size && p.size[0]) || 360, fills: [paint("window_bg")], cornerRadius: 8,
    strokePaint: paint("border"), strokeWeight: 1, layoutMode: "VERTICAL",
    paddingLeft: 12, paddingTop: 10, paddingRight: 12, paddingBottom: 12, itemSpacing: 8,
    imgui: { role: "window", props: { title: p.title, size: p.size, pos: p.pos } },
  }, [deco(T(p.title || "Window", 16, "text")), ...(win.children || []).map(widgetToFig).filter(Boolean)]);
}

function widgetToFig(node) {
  if (!node || typeof node !== "object") return null;
  const t = node.type, p = node.props || {}, kids = Array.isArray(node.children) ? node.children : [];
  const meta = (role = "widget", props = p) => ({ type: t, props, role });
  const labeled = (inner) => F(`${t}: ${p.label || ""}`, { layoutMode: "VERTICAL", itemSpacing: 3, imgui: meta() },
    [...(p.label ? [deco(T(p.label, 12, "text_dim"))] : []), inner]);

  switch (t) {
    case "text": return Object.assign(T(p.text || "", 14, "text"), { imgui: meta() });
    case "separator": return F("separator", { w: 320, h: 1, fills: [paint("border")], imgui: meta() });
    case "same_line": return null;                      // layout hint, no Figma node
    case "spacing": return F("spacing", { w: 1, h: 6, imgui: meta() });
    case "button":
      return F(`button: ${p.label}`, {
        w: (p.size && p.size[0]) || 120, h: 26, cornerRadius: 4, fills: [paint("frame")],
        strokePaint: paint("border"), strokeWeight: 1, layoutMode: "HORIZONTAL",
        primaryAxisAlignItems: "CENTER", counterAxisAlignItems: "CENTER", imgui: meta(),
      }, [T(p.label || "Button", 14, "text")]);
    case "checkbox":
      return F(`checkbox: ${p.label}`, { layoutMode: "HORIZONTAL", itemSpacing: 8, counterAxisAlignItems: "CENTER", imgui: meta() },
        [F("box", { w: 16, h: 16, cornerRadius: 3, fills: [paint(p.value ? "accent" : "frame")], strokePaint: paint("border"), strokeWeight: 1 }),
         T(p.label || "", 14, "text")]);
    case "slider_float":
    case "slider_int": {
      const min = +p.min || 0, max = (p.max == null ? (t === "slider_int" ? 100 : 1) : p.max), val = +p.value || 0;
      const frac = max > min ? Math.max(0, Math.min(1, (val - min) / (max - min))) : 0;
      return labeled(F("track", { w: 300, h: 6, cornerRadius: 3, fills: [paint("frame")] },
        [F("fill", { w: Math.round(300 * frac), h: 6, cornerRadius: 3, fills: [paint("accent")] })]));
    }
    case "input_text":
    case "input_int":
    case "input_float":
      return labeled(F("box", { w: p.width || 220, h: 24, cornerRadius: 4, fills: [paint("frame")], strokePaint: paint("border"), strokeWeight: 1,
        layoutMode: "HORIZONTAL", counterAxisAlignItems: "CENTER", paddingLeft: 8 }, [T(String(p.value ?? ""), 14, "text")]));
    case "combo":
    case "listbox":
      return labeled(F("box", { w: 200, h: 24, cornerRadius: 4, fills: [paint("frame")], strokePaint: paint("border"), strokeWeight: 1,
        layoutMode: "HORIZONTAL", counterAxisAlignItems: "CENTER", primaryAxisAlignItems: "SPACE_BETWEEN", paddingLeft: 8, paddingRight: 8 },
        [T((p.items || [])[p.value || 0] || "", 14, "text"), T("▾", 12, "text_dim")]));
    case "color_edit3": case "color_edit4": case "color_picker3": case "color_picker4": case "color_button": {
      const v = Array.isArray(p.value) ? p.value : [1, 1, 1, 1];
      return labeled(F("swatch", { w: 40, h: 20, cornerRadius: 3, fills: [{ color: { r: v[0], g: v[1], b: v[2] }, opacity: 1 }], strokePaint: paint("border"), strokeWeight: 1 }));
    }
    case "tab_bar":
      return F("tab_bar", { layoutMode: "VERTICAL", itemSpacing: 8, imgui: meta("tab_bar") },
        kids.filter((k) => k.type === "tab_item").map(tabItemToFig));
    case "collapsing_header":
    case "group":
    case "child":
      return F(`${t}: ${p.label || p.name || ""}`, {
        w: (p.size && p.size[0]) || 320, fills: [paint("surface")], cornerRadius: 6, strokePaint: paint("border"), strokeWeight: 1,
        layoutMode: "VERTICAL", paddingLeft: 10, paddingTop: 8, paddingRight: 10, paddingBottom: 10, itemSpacing: 8, imgui: meta(),
      }, [...(p.label ? [deco(T(p.label, 12, "text_dim"))] : []), ...kids.map(widgetToFig).filter(Boolean)]);
    default:
      return F(`${t}`, { layoutMode: "HORIZONTAL", paddingLeft: 4, paddingTop: 2, imgui: meta() }, [T(p.label || p.text || t, 13, "text")]);
  }
}

function tabItemToFig(tab) {
  const tp = tab.props || {};
  return F(`tab: ${tp.label}`, {
    w: 320, fills: [paint("surface")], cornerRadius: 6, strokePaint: paint("border"), strokeWeight: 1,
    layoutMode: "VERTICAL", paddingLeft: 10, paddingTop: 8, paddingRight: 10, paddingBottom: 10, itemSpacing: 8,
    imgui: { role: "tab_item", type: "tab_item", props: { label: tp.label } },
  }, [deco(T(tp.label || "Tab", 12, "accent")), ...(tab.children || []).map(widgetToFig).filter(Boolean)]);
}

// ---------------- figma -> spec ----------------
export function figmaToSpec(model) {
  const windows = [];
  const roots = (model?.imgui?.role === "root") ? (model.children || []) : [model];
  for (const r of roots) { const w = nodeToWindow(r); if (w) windows.push(w); }
  return { windows };
}

function nodeToWindow(n) {
  if (n?.imgui?.role === "window") return { type: "window", props: n.imgui.props || {}, children: childrenToSpec(n) };
  const kids = childrenToSpec(n);
  return kids.length ? { type: "window", props: { title: n.name || "Imported" }, children: kids } : null;
}

function childrenToSpec(node) {
  const out = [];
  for (const c of node.children || []) { const s = nodeToSpec(c); if (s) out.push(s); }
  return out;
}

const CONTAINERS = ["group", "child", "collapsing_header"];

function nodeToSpec(n) {
  const c = n?.imgui;
  if (c && c.role === "decoration") return null;
  if (!c) {
    if (n.type === "TEXT") return { type: "text", props: { text: n.characters || "" } };
    const k = childrenToSpec(n);
    return k.length ? { type: "group", children: k } : null;
  }
  if (c.role === "tab_bar") return { type: "tab_bar", children: childrenToSpec(n) };
  if (c.role === "tab_item") return { type: "tab_item", props: c.props || {}, children: childrenToSpec(n) };
  if (c.role === "window") return { type: "window", props: c.props || {}, children: childrenToSpec(n) };
  if (c.role === "widget") {
    if (CONTAINERS.includes(c.type)) return { type: c.type, props: c.props || {}, children: childrenToSpec(n) };
    return { type: c.type, props: c.props || {} };
  }
  const k = childrenToSpec(n);
  return k.length ? { type: "group", children: k } : null;
}
