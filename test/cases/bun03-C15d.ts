// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext" } }
// @filename: a.ts
function f() { import { c } from "./missing"; export { c }; }
export {};
const sentinel: number = "";
