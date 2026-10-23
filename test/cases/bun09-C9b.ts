// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { k: "i"; p: this extends { k: "i" } ? 1 : 2 }
function fi<T extends I>(t: T) { const a: 1 = t.p; }
const sentinel: number = "";
