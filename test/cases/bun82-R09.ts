// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o1 = { m(this) { return this; } };
const t1: null = o1;
const o2 = { m(this) { return 1; }, k: 1 };
const t2: null = o2;
const o3 = { f: function (this) { return this; } };
const t3: null = o3;
const o4 = { m(this, x: number) { return x; } };
o4.m = 1;
const sentinel: number = "";
