// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface B1 { (x: number): void; p: number }
interface A1 extends B1 {}
interface A1 extends B1 {}
declare const a1: A1;
a1("s");
const sentinel: number = "";
