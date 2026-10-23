// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext" } }
// @filename: a.ts
import "./m";
export const o = { m() { export * as ns from "./m"; export * as ns from "./m"; } };
export const o3 = { m() { export * as ns from "./m"; } };
o3.m.ns.v;
export const o4 = { m() { export default 1; export default 2; } };
export const sentinel: number = "";
// @filename: m.ts
export const v = 1;
