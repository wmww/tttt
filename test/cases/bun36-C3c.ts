// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "node16" }, "files": ["a.ts"] }
// @filename: a.ts
import w3 = require("tv");
const sentinel: number = "";
// @filename: node_modules/tv/d.json
{"a":1}
// @filename: node_modules/tv/index.d.ts
export declare const which: "tv/index";
// @filename: node_modules/tv/package.json
{"name":"tv","version":"1.0.0","types":"index.d.ts","typesVersions":{"*":{"index.d.ts":["d.json"]}}}
