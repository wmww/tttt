// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"], "references": [{ "path": "./lib" }] }
// @filename: a.ts
import d from "./lib/types";
const sentinel: number = "";
// @filename: lib/tsconfig.json
{ "compilerOptions": { "composite": true, "strict": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "outDir": "dist" }, "files": ["types.d.ts"] }
// @filename: lib/types.d.ts
export declare const a: number;
