// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "rootDir": "src", "types": [], "lib": ["es2022"] }, "files": ["src/a.ts"] }
// @filename: node_modules/p/g.ts
declare const g: 1;
// @filename: node_modules/p/index.ts
/// <reference path="./g.ts" />
export const p = 1;
// @filename: node_modules/p/package.json
{"name":"p","version":"1.0.0","types":"index.ts"}
// @filename: src/a.ts
/// <reference path="./b.ts" />
import "p";
export const sentinel: number = "";
// @filename: src/b.ts
/// <reference path="../node_modules/p/index.ts" />
