// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "commonjs" } }
// @filename: b.js
NS = { x: 1 };
// @filename: g.d.ts
declare namespace NS { const x: number }
// @filename: z.ts
const sentinel: number = "";
