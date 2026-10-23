// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext" } }
// @filename: a.cts
import m1 from "./esm.mjs";
import { default as m2 } from "./esm.mjs";
export { default as m3 } from "./esm.mjs";
const sentinel: number = "";
// @filename: esm.mts
const me = { q: 1 };
export { me as "module.exports" };
