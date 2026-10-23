// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "resolveJsonModule": true, "esModuleInterop": true, "module": "commonjs", "moduleResolution": "bundler" } }
// @filename: a.ts
import * as ns from "./d.json";
import d from "./d.json";
const n: 0 = d; const m: 0 = ns;
const sentinel: number = "";
// @filename: d.json
module.exports = { "a": 1 }
