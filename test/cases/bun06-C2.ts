// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "commonjs" } }
// @filename: b.js
/** @type {() => Generator<number, void, string>} */
function* j1() {
  const x = yield "a";
}
// @filename: z.ts
const sentinel: number = "";
