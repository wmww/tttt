// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function id<T>(x: T): T;
const n1 = { m() { return this.z; }, f() { return this.z; }, g: () => id(2 + {}) };
const n5 = { m() { return this.z; }, f() { return this.z; }, g: () => id(id(6 + {})) };
const n9 = { m() { return this.z; }, f() { return this.z; }, g: () => [id(7 + {})] };
const n11 = { m() { return this.z; }, f() { return this.z; }, g: () => id({ a: 9 + {} }) };
const sentinel: number = "";
