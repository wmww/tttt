// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function id<T>(x: T): T; declare function abs(n: number): number;
const a = { m() { return this.z; }, f() { return this.z; }, g: id(() => ({}).nope) };
const b = { m() { return this.z; }, f() { return this.z; }, g: id(() => nope) };
const c = { m() { return this.z; }, f() { return this.z; }, g: id(() => abs("")) };
const d = { m() { return this.z; }, f() { return this.z; }, g: id(function () { return abs(""); }) };
const e = { m() { return this.z; }, f() { return this.z; }, g: id({ h() { return abs(""); } }) };
// not an argument of a generic call: both tools lose the error
const h = { m() { return this.z; }, f() { return this.z; }, g: [() => abs("")] };
const sentinel: number = "";
