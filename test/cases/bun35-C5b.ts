// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import "e2/a";
const sentinel: number = "";
// @filename: node_modules/e2/a.d.ts
export {};
// @filename: node_modules/e2/package.json
{ "exports": { "./a": "./a.d.ts", "#x": "./a.d.ts" } }
