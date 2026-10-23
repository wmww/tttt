// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "dom"], "libReplacement": true, "allowJs": true }, "files": ["a.ts"] }
// @filename: a.ts
const d: Document = 1;
const sentinel: number = "";
// @filename: node_modules/@typescript/lib-dom/index.js
module.exports = {}
// @filename: node_modules/@typescript/lib-dom/package.json
{"name":"@typescript/lib-dom","version":"1.0.0","main":"index.js"}
