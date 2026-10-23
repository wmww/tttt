// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import "e1";
const sentinel: number = "";
// @filename: node_modules/e1/a.d.ts
export {};
// @filename: node_modules/e1/package.json
{ "exports": { "#x": "./a.d.ts", "default": "./a.d.ts" } }
