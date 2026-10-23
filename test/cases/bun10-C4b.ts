// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface B1 { (x: number): void; p: number }
interface A1 extends B1 {}
interface A1 extends B1 {}
interface A3 { (x: number): void; p: number }
declare var v1: A1;
declare var v1: A3;
const sentinel: number = "";
