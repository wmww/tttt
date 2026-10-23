// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "declaration": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts", "b.ts", "u.ts"] }
// @filename: a.ts
import { o } from "./u";
export const h = o;
const sentinel: number = "";
// @filename: b.ts
let v;
v = 1;
// @filename: u.ts
// @ts-nocheck
export const o = { g() { return v; } };
