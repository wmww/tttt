// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends Record<string, number>>(o: Readonly<T>, k: string) {
  o[k] += 1;
}
const sentinel: number = "";
