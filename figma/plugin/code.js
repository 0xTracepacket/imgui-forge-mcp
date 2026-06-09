// Figma plugin main thread: paint a node model from the bridge onto the canvas,
// and serialize the canvas back to a node model. The WebSocket lives in ui.html.

figma.showUI(__html__, { width: 300, height: 240 });

const FONT = { family: "Inter", style: "Regular" };
const KEY = "imgui";
let autoPush = false;
let pushTimer = null;

function solid(paint) {
  if (!paint || !paint.color) return null;
  return { type: "SOLID", color: paint.color, opacity: paint.opacity == null ? 1 : paint.opacity };
}

async function paintNode(model, parent) {
  let node;
  if (model.type === "TEXT") {
    node = figma.createText();
    node.fontName = FONT;
    node.characters = String(model.characters || "");
    if (model.fontSize) node.fontSize = model.fontSize;
    const tp = solid(model.textPaint);
    if (tp) node.fills = [tp];
  } else {
    node = figma.createFrame();
    node.clipsContent = false;
    node.fills = (model.fills || []).map(solid).filter(Boolean);
    if (model.cornerRadius != null) node.cornerRadius = model.cornerRadius;
    const sp = solid(model.strokePaint);
    if (sp) { node.strokes = [sp]; node.strokeWeight = model.strokeWeight || 1; }
  }
  node.name = model.name || model.type;
  parent.appendChild(node);

  const isAuto = (model.type === "FRAME" && !!model.layoutMode);
  if (isAuto) {
    node.layoutMode = model.layoutMode;
    node.paddingLeft = model.paddingLeft || 0;
    node.paddingRight = model.paddingRight || 0;
    node.paddingTop = model.paddingTop || 0;
    node.paddingBottom = model.paddingBottom || 0;
    node.itemSpacing = model.itemSpacing || 0;
    if (model.primaryAxisAlignItems) node.primaryAxisAlignItems = model.primaryAxisAlignItems;
    if (model.counterAxisAlignItems) node.counterAxisAlignItems = model.counterAxisAlignItems;
    const bothFixed = !!(model.w && model.h);
    node.primaryAxisSizingMode = bothFixed ? "FIXED" : "AUTO";
    node.counterAxisSizingMode = bothFixed ? "FIXED" : "AUTO";
  }

  for (const c of model.children || []) await paintNode(c, node);

  // resize only when safe (non-autolayout, or autolayout with both dims fixed)
  if ((model.w || model.h) && node.type !== "TEXT") {
    const canResize = !isAuto || (model.w && model.h);
    if (canResize) {
      const w = model.w || node.width || 1, h = model.h || node.height || 1;
      try { node.resize(Math.max(1, w), Math.max(1, h)); } catch (e) {}
    }
  }
  if (model.visible === false) node.visible = false;

  if (model.imgui) node.setPluginData(KEY, JSON.stringify(model.imgui));
  return node;
}

async function paintModel(model) {
  await figma.loadFontAsync(FONT);
  for (const n of figma.currentPage.children) {
    const pd = n.getPluginData(KEY);
    if (pd && JSON.parse(pd).role === "root") n.remove();
  }
  const root = await paintNode(model, figma.currentPage);
  figma.viewport.scrollAndZoomIntoView([root]);
  figma.notify("Pulled from ImGui");
}

function serializeNode(node) {
  const out = { type: node.type === "TEXT" ? "TEXT" : "FRAME", name: node.name };
  const pd = node.getPluginData(KEY);
  if (pd) { try { out.imgui = JSON.parse(pd); } catch (e) {} }
  if (node.type === "TEXT") out.characters = node.characters;
  if ("children" in node) out.children = node.children.map(serializeNode);
  return out;
}

function findExportRoot() {
  const sel = figma.currentPage.selection;
  if (sel.length === 1) return sel[0];
  for (const n of figma.currentPage.children) {
    const pd = n.getPluginData(KEY);
    if (pd) { const c = JSON.parse(pd); if (c.role === "root" || c.role === "window") return n; }
  }
  return figma.currentPage.children[0] || null;
}

function pushToImGui() {
  const root = findExportRoot();
  if (!root) { figma.notify("Nothing to push — pull or design first"); return; }
  figma.ui.postMessage({ type: "send", payload: { kind: "figmodel", model: serializeNode(root) } });
}

figma.ui.onmessage = async (msg) => {
  try {
    if (msg.type === "paint") await paintModel(msg.model);
    else if (msg.type === "push") pushToImGui();
    else if (msg.type === "setAuto") { autoPush = !!msg.value; figma.notify("Auto-push: " + (autoPush ? "ON" : "OFF")); }
  } catch (e) {
    console.error("[imgui-tool]", e);
    figma.notify("imgui-tool: " + (e && e.message ? e.message : String(e)), { error: true });
  }
};

figma.on("documentchange", () => {
  if (!autoPush) return;
  if (pushTimer) clearTimeout(pushTimer);
  pushTimer = setTimeout(pushToImGui, 400);
});
