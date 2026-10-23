// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "nm";
class C2b { nm = 1; [k] = 1; }
const sentinel: number = "";
