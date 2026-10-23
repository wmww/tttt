// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare namespace N { const o: { get a() {} } }
declare namespace N3 { type U = { get a() { let x = 1; return x; } } }
declare const p: { set a(v: number) {} };
const q: { get a() { return 1; } } = { a: 1 };
const sentinel: number = "";
