// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const s: string;
interface I7 { [s]: number }
interface I7 { [s]: string }
declare const i7: I7;
const x: number = i7["zz"];
const sentinel: number = "";
