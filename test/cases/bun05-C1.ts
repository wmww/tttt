// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f1<T extends Record<string, number>>(obj: T, k: string) {
  obj[k] += 1;
}
const sentinel: number = "";
