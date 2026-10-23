// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import "e3/a";
const sentinel: number = "";
// @filename: node_modules/e3/a.d.ts
export {};
// @filename: node_modules/e3/package.json
{ "exports": { "": "./a.d.ts", "./a": "./a.d.ts" } }
