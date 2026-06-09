// Neutral dark palette used to color exported Figma nodes.
export const TOKENS = {
  window_bg: [0.12, 0.12, 0.14, 1],
  surface:   [0.16, 0.16, 0.19, 1],
  frame:     [0.20, 0.20, 0.24, 1],
  text:      [0.90, 0.91, 0.93, 1],
  text_dim:  [0.60, 0.62, 0.66, 1],
  accent:    [0.26, 0.59, 0.98, 1],
  border:    [1, 1, 1, 0.10],
};

// token name or [r,g,b,a] -> Figma SolidPaint form
export function paint(token) {
  const c = Array.isArray(token) ? token : (TOKENS[token] || [1, 1, 1, 1]);
  return { color: { r: c[0], g: c[1], b: c[2] }, opacity: c[3] == null ? 1 : c[3] };
}
