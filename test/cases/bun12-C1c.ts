// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C<T, U> {
  m(): { [K in keyof T]: U } { return null! }
  n(c: C<U, { a: 1 }>) {
    const a: { a: 1 } = c.m().a;
  }
}
const sentinel: number = "";
