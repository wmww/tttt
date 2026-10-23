// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C<T> {
  m<U>(u: U) { return u; }
  static sm<U>(u: U) { return u; }
  a = (<U,>(u: U) => u)<1>;
  b = (function <U>(u: U) { return u; })<2>;
  c = ({ m<U>(u: U): U { return u; } }).m<3>;
  d = this.m<4>;
  e = C.sm<5>;
}
let a: C<string>["a"] | C<boolean>["a"] = 1; let b: C<string>["b"] | C<boolean>["b"] = 1; let c: C<string>["c"] | C<boolean>["c"] = 1;
let d: C<string>["d"] | C<boolean>["d"] = 1; let e: C<string>["e"] | C<boolean>["e"] = 1;
const sentinel: number = "";
