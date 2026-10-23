// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function nu<T extends number | undefined, U extends string>(o: { p: T | `a${U}` | Uppercase<U> }) {
  const { p = 1 } = o;
  const back: number | `a${U}` | Uppercase<U> = p;
}
const sentinel: number = "";
