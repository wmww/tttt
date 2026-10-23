// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const k: string;
type T19 = C17["q"];
class C17 { [k] = 1; p: T19 = 1; q = 1 }
const sentinel: number = "";
