// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import "z";
const sentinel: number = "";
// @filename: node_modules/z/index.d.ts
export {};
// @filename: node_modules/z/package.json
{ "exports": 0 }
