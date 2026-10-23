// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import "pkg/p/..\\b";
const sentinel: number = "";
// @filename: node_modules/pkg/b.d.ts
export {};
// @filename: node_modules/pkg/package.json
{ "exports": { "./p/*": "./p/*.d.ts" } }
