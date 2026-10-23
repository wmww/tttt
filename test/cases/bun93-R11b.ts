// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class D<T> { n<U>(u: U, t: T): T }
const mk = <T,>(d: D<T>) => d.n<2>;
mk(null! as D<string>)(2, "x");
const r: string = mk(null! as D<string>)(2, null!);
class C<T> {
  n<U>(u: U, t: T) { return t; }
  q = this.n<2>;
}
new C<string>().q(2, "x");
function dflt<T>(d: D<T>, f = d.n<2>) { return f; }
dflt(null! as D<string>)(2, "x");
// with a block on the way to the type parameter both tools instantiate
function blk<T>(d: D<T>) { return d.n<2>; }
blk(null! as D<string>)(2, "x");
const sentinel: number = "";
