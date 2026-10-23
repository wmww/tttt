// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import {} from "e";
const sentinel: number = "";
// @filename: node_modules/e/index.js
// @filename: node_modules/e/package.json
{"name":"","version":"1.0.0"}
