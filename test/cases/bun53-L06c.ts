// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "declaration": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts", "u.ts"] }
// @filename: a.ts
let v;
v = 1;
const h = o;
const sentinel: number = "";
// @filename: u.ts
// @ts-nocheck
const o = { g() { return v; } };
