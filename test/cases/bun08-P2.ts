// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "rewriteRelativeImportExtensions": true, "noEmit": true }, "files": ["node_modules/pk/src.ts", "a.ts"] }
// @filename: a.ts
import { b } from "pk/src.ts";
const sentinel: number = "";
// @filename: node_modules/pk/package.json
{ "name": "pk", "version": "1.0.0" }
// @filename: node_modules/pk/src.ts
export const b = 1;
