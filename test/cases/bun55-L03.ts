// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function box<T extends { a: string | number }>(o: T): T;
declare function both<T>(o: T & { a: string | number }): T;
declare function key<K extends PropertyKey>(o: { key: K }): K;
class C {
  r!: string | number;
  m(x: this["r"]) {
    let u = both({ a: x });
    u = 1;
    if (typeof x === "string") return;
    const n: number = box({ a: x }).a;
    let v = box({ a: x });
    v = 1;
  }
  n(name: keyof this) {
    let got = key({ key: name });
    got = 1;
  }
}
const sentinel: number = "";
