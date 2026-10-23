// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "maxNodeModuleJsDepth": 3 }, "files": ["a.ts"] }
// @filename: a.ts
import { vx, px } from "x"; import { vy } from "y";
const n1: null = vx; const n2: null = px; const n3: null = vy;
const sentinel: number = "";
// @filename: node_modules/deep/index.js
exports.d = 1;
// @filename: node_modules/deep/package.json
{"name":"deep","version":"1.0.0","main":"index.js"}
// @filename: node_modules/x/index.js
const m = require("mid"); const p = require("p"); exports.vx = m.v; exports.px = p.pv;
// @filename: node_modules/x/node_modules/mid/index.js
const d = require("deep"); exports.v = d.d;
// @filename: node_modules/x/node_modules/mid/package.json
{"name":"mid","version":"1.0.0","main":"index.js"}
// @filename: node_modules/x/node_modules/p/index.js
const m = require("mid"); exports.pv = m.v;
// @filename: node_modules/x/node_modules/p/package.json
{"name":"p","version":"1.0.0","main":"index.js"}
// @filename: node_modules/x/package.json
{"name":"x","version":"1.0.0","main":"index.js"}
// @filename: node_modules/y/index.js
const p = require("p"); exports.vy = p.pv;
// @filename: node_modules/y/node_modules/p/index.js
const m = require("mid"); exports.pv = m.v;
// @filename: node_modules/y/node_modules/p/package.json
{"name":"p","version":"1.0.0","main":"index.js"}
// @filename: node_modules/y/package.json
{"name":"y","version":"1.0.0","main":"index.js"}
