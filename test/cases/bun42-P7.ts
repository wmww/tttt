// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import {} from "dep/sub/";
const sentinel: number = "";
// @filename: node_modules/dep/package.json
{ "name": "dep", "exports": { "./sub": "./sub.d.ts" } }
// @filename: node_modules/dep/sub.d.ts
export {};
