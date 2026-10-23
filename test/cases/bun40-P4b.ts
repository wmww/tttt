// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "maxNodeModuleJsDepth": 1, "types": [], "lib": ["es2022"] }, "files": ["src/a.ts"] }
// @filename: node_modules/p/index.js
const u = require("./util");
exports.p = u.u;
// @filename: node_modules/p/package.json
{"name":"p","version":"1.0.0","main":"index.js"}
// @filename: node_modules/p/util.js
exports.u = 1;
// @filename: src/a.ts
/// <reference path="./b.ts" />
import { p } from "p";
const q: 0 = p;
export const sentinel: number = "";
// @filename: src/b.ts
/// <reference path="../node_modules/p/index.js" />
