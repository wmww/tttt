// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const other: Foo<string>;
class Foo<T> {
  m<U>(b: U, t: T): void {
    other.m = <V>(b: U, t: string): void => {};
  }
}
const sentinel: number = "";
