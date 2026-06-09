// Verify spec -> figma model -> spec preserves the widget tree.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { specToFigma, figmaToSpec } from "./convert.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const orig = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "spec", "ui_spec.json"), "utf8"));
const back = figmaToSpec(specToFigma(orig));

// count interactive widgets (same_line is a layout hint and is not represented in Figma)
function count(spec) {
  let n = 0;
  (function walk(nodes) {
    for (const x of nodes || []) {
      if (x.type !== "same_line") n++;
      walk(x.children);
    }
  })(spec.windows.flatMap((w) => w.children || []));
  return { windows: spec.windows.length, nodes: n };
}

const a = count(orig), b = count(back);
console.log("ORIGINAL:", JSON.stringify(a));
console.log("ROUNDTRIP:", JSON.stringify(b));
const ok = a.windows === b.windows && a.nodes === b.nodes;
console.log(ok ? "\nROUND-TRIP OK (tree preserved)" : "\nMISMATCH");
process.exit(ok ? 0 : 1);
