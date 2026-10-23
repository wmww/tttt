// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
async function f() {
  (await import(("./m"))).nope;
  (await import(("./missing"))).nope;
}
const sentinel: number = "";
// @filename: m.ts
export const a = 1;
