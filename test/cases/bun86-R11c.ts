// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "dom"], "libReplacement": true, "module": "nodenext" }, "files": ["a.ts"] }
// @filename: a.ts
doc.a = "";
const sentinel: number = "";
// @filename: node_modules/@typescript/lib-dom/index.d.ts
import { X } from "./other";
declare global { var doc: X }
// @filename: node_modules/@typescript/lib-dom/other.d.ts
export interface X { a: number }
// @filename: node_modules/@typescript/lib-dom/package.json
{"name":"@typescript/lib-dom","version":"1.0.0","type":"module","types":"index.d.ts"}
