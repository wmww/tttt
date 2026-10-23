// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext", "allowArbitraryExtensions": true } }
// @filename: a.ts
import * as p from "./p.json" with { type: "json" };
const t: 0 = p;
const sentinel: number = "";
// @filename: p.d.json.ts
export {};
// @filename: package.json
{ "type": "module" }
