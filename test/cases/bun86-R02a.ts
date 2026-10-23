// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "dom"], "libReplacement": true, "allowJs": true, "checkJs": true, "skipLibCheck": true }, "files": ["a.ts"] }
// @filename: a.ts
Document.a = 2;
const sentinel: number = "";
// @filename: node_modules/@typescript/lib-dom/index.js
var Document = { a: 1 };
/** @type {number} */ var bad = "";
function helper(x) { return x; }
class Win { constructor() { this.p = 1; } }
// @filename: node_modules/@typescript/lib-dom/package.json
{"name":"@typescript/lib-dom","version":"1.0.0","main":"index.js"}
