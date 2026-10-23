// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["src/a.ts"] }
// @filename: src/a.ts
import {} from "c:/x";
const sentinel: number = "";
// @filename: src/c:/x.ts
export {};
