// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function abs(n: number): number; declare let u: (() => number) | undefined;
const a = { m() { return this.z; }, f() { return this.z; }, g() { const q = (1, 2); return q; } };
const b = { m() { return this.z; }, f() { return this.z; }, g() { return abs(1) instanceof Object; } };
const c = { m() { return this.z; }, f() { return this.z; }, g() { return u(); } };
const d = { m() { return this.z; }, f() { return this.z; }, g() { return "" satisfies number; } };
const e = { m() { return this.z; }, f() { return this.z; }, g() { return { ...1 }; } };
const h = { m() { return this.z; }, f() { return this.z; }, g() { return new (class { private constructor() {} })(); } };
const i = { m() { return this.z; }, f() { return this.z; }, g() { return g2 ? 1 : 2; function g2() {} } };
const j = { m() { return this.z; }, f() { return this.z; }, g() { return /(/; } };
const k = { m() { return this.z; }, f() { return this.z; }, g() { return later; let later = 1; } };
const sentinel: number = "";
