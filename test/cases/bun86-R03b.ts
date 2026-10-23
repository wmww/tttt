// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true }, "files": ["a.d.ts", "b.js", "c.ts"] }
// @filename: a.d.ts
declare namespace C { interface x { j: 1 } }
// @filename: b.js
class C {}
C.x = "s";
// @filename: c.ts
const sentinel: number = "";
