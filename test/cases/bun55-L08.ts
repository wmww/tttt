// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type U = { kind: 0; a: string } | { kind: 1; b: number };
function f1(x: U, v: 0 | 1) {
  if ((x.kind) = v) {
    x.b;
  } else {
    x.a;
  }
  if ((x["kind"]) ||= v) {
    x.b;
  }
}
// without the parentheses both narrow
function f2(x: U, v: 0 | 1) {
  if (x.kind = v) {
    x.b;
  }
}
const sentinel: number = "";
