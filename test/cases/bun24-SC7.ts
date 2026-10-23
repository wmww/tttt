// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C<T> {
  m<U>(u: U): [T, U] { return null! }
  n() { const c = null! as C<T[]>; const x2: string = c.m<number>; }
}
const sentinel: number = "";
