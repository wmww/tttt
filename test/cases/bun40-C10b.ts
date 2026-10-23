// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "dom"], "libReplacement": true } }
// @filename: a.ts
interface Order { f(x: "a"): 3 }
declare const o: Order; o.f(0);
const sentinel: number = "";
// @filename: node_modules/@typescript/lib-dom/extra.d.ts
interface Order { f(x: "extra"): 2 }
// @filename: node_modules/@typescript/lib-dom/index.d.ts
/// <reference path="./extra.d.ts" />
interface Order { f(x: "dom"): 1 }
// @filename: node_modules/@typescript/lib-dom/package.json
{"name":"@typescript/lib-dom","version":"1.0.0","types":"index.d.ts"}
