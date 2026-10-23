// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext" } }
// @filename: a.ts
interface I {}
export default (I);
const sentinel: number = "";
