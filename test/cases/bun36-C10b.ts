// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { which } from "./inner\\";
const x: 0 = which;
const sentinel: number = "";
// @filename: inner.d.ts
export declare const which: "inner.d.ts";
// @filename: inner/index.d.ts
export declare const which: "inner/index";
