// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f(): "a";
class C14 { [k: string]: number; [f()]() {} }
class C15 { [k: string]: number; get [f()]() { return ""; } }
const sentinel: number = "";
