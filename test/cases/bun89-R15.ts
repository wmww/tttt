// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class U {
  #p = 1;
  m(x: U | { [k: string]: string }, y: { [k: string]: string }, z: U & { [k: string]: string }, w: U | Record<string, boolean>, t: U | [string]) {
    const r1: never = x.#p;
    const r2: never = y.#p;
    const r3: never = z.#p;
    const r4: never = w.#p;
    const r5: never = t.#p;
    x.#p = 1;
    x.#p = "";
  }
}
const sentinel: number = "";
