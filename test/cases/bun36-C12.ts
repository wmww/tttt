// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import {} from "n";
const sentinel: number = "";
// @filename: node_modules/n/index.d.ts
export {};
// @filename: node_modules/n/main.d.ts
export {};
// @filename: node_modules/n/package.json
{"name":"n","version":"1.0.0","types":"main.d.ts/"}
