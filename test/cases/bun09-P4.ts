// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type D<X> = X extends PropertyKey ? 1 : 2;
type W<X> = X extends number ? [X] : never;
function f<T>(w: W<D<keyof T>>, a: D<keyof T>) {
  const probe: unknown[] = w;
  const x: 1 = a;
}
const sentinel: number = "";
