// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
import { K } from "./m";
export function f1() { const u3 = 1; export { u3 }; }
export function f4() { export { K as KK }; }
export function f7() { const u = 1; export default u; }
export function f8() { export default { a: 1, a: 2 }; }
const sentinel: number = "";
// @filename: m.ts
export class K {}
