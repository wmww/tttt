// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "typeRoots": ["./types"], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import "modroot/.";
const sentinel: number = "";
// @filename: types/modroot/index.d.ts
export {};
