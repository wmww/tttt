// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "commonjs" } }
// @filename: a.js
/** @param {(a: number) => void} cb */
function takes(cb) {}
takes((a = []) => {});
// @filename: z.ts
const sentinel: number = "";
