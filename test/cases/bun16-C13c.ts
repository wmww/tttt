// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type S = string | Promise<S>;
declare const s: S;
async function f() {
  await s;
  await s;
}
const sentinel: number = "";
