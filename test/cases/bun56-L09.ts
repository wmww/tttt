// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const v: any;
interface I {}
class X extends import<string>, v {}
class Y implements import<string>, {}
class Z implements I implements import<string>, I {}
const sentinel: number = "";
