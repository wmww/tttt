// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
async function run<T extends () => unknown>(f: T): Promise<Awaited<ReturnType<T>>> {
  return await (f() as ReturnType<T>);
}
const sentinel: number = "";
