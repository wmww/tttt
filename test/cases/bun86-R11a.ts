// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "dom"], "libReplacement": true, "skipLibCheck": true }, "files": ["a.ts"] }
// @filename: a.ts
document.title = 2; const d: Document = { title: 3 };
const sentinel: number = "";
// @filename: node_modules/@typescript/lib-dom/index.ts
interface Document { title: string }
var document: Document = { title: 1 };
// @filename: node_modules/@typescript/lib-dom/package.json
{"name":"@typescript/lib-dom","version":"1.0.0","types":"index.ts"}
