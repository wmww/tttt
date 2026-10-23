// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "rewriteRelativeImportExtensions": true }, "files": ["src/a.ts"] }
// @filename: src/a.ts
import "../SRC/foo.ts";
const sentinel: number = "";
// @filename: src/foo.ts.ts
export {};
