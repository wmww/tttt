// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const s: string;
class A { [s] = 1; }
interface A { b: boolean }
const sentinel: number = "";
