// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function gen<T>(cb: (x: T) => void): AsyncGenerator<never, T, unknown>;
async function* f3(): AsyncGenerator<never, void, unknown> {
  const x: number | Promise<number> = yield* gen(x => { const n: number = x; });
}
const sentinel: number = "";
