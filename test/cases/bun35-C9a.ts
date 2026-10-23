// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import "p//sub";
const sentinel: number = "";
// @filename: b.d.ts
export {};
// @filename: package.json
{ "name": "p", "exports": { "./sub": "./b.d.ts" } }
