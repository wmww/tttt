// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true } }
// @filename: b.js
function h1(@dec x) {}
function h2(export @dec x) {}
function h3(export default @dec x) {}
class K { m(a, export @dec y) {} }
// @filename: z.ts
const sentinel: number = "";
