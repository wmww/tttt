// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface U2 extends U1, U3 {}
interface U1 extends U2 {}
interface U3 { m(): this }
declare const u1: U1;
const k1: number = u1.m();
const sentinel: number = "";
