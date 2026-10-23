// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "declaration": true, "noEmit": true, "types": [], "lib": ["es5"] }, "files": ["a.ts", "u.ts"] }
// @filename: a.ts
import { o } from "./u";
export const h = o;
const sentinel: number = "";
// @filename: u.ts
// @ts-nocheck
export const o = { *g() { yield 1; } };
