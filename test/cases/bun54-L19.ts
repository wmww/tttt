// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare function assert(c: unknown): asserts c;
const r = h("a");
function h(x: string) {
  if (typeof x !== "string") {
    assert(x === k);
    return x;
  }
  return 1;
}
const k = r;
const sentinel: number = "";
// @filename: b.ts
export {};
declare function assert(c: unknown): asserts c;
const r1 = h1("a");
function h1(x: string) {
  if (typeof x !== "string") { assert(x instanceof k1); return x; }
  return class {};
}
const k1 = r1;
const r2 = h2("a");
function h2(x: string) {
  if (typeof x !== "string") { assert(k2 in x); return x; }
  return "p";
}
const k2 = r2;
const r3 = h3("a");
function h3(x: string) {
  if (typeof x !== "string") { assert(x !== k3); return x; }
  return 1;
}
const k3 = r3;
const r4 = h4("a");
function h4(x: string) {
  if (typeof x !== "string") { assert(true && x === k4); return x; }
  return 1;
}
const k4 = r4;
const r5 = h5(null!);
function h5(x: never) {
  assert(x === k5);
  return [x];
}
const k5 = r5;
const r6 = h6("a");
function h6(x: string) {
  if (typeof x !== "string") { assert(!(x === k6)); return x; }
  return 1;
}
const k6 = r6;
const sentinel: number = "";
