// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function h<T, U>(t: T, u: U): T extends string ? [T, U] : never {
  const r = h(null! as U | string, 1);
  const bad: boolean = r;
  return null!;
}
const sentinel: number = "";
