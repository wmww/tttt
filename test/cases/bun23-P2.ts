// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.mts
export const x = require("pkg");
const sentinel: number = "";
// @filename: g.d.ts
declare function require(id: string): import("pkg", { with: { "resolution-mode": "require" } }).Foo;
// @filename: node_modules/pkg/index.d.ts
export interface Foo { a: 1 }
// @filename: node_modules/pkg/package.json
{ "name": "pkg", "exports": { "require": { "types": "./index.d.ts" } } }
