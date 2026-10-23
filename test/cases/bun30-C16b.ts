// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const sentinel: number = "";
// @filename: d.ts
import "./m";
function f() { export * as ns from "./m"; }
f.ns;
// @filename: m.ts
export const m1 = 1;
