// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "commonjs" } }
// @filename: a.js
#!/usr/bin/env node
exports.a;
module.exports = { a: exports };
// @filename: b.ts
const sentinel: number = "";
