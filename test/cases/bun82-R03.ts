// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
type Item = { id: number; parent: Item | undefined; label: string };
function walk(start: Item) {
  let item: Item | undefined = start;
  while (item) {
    const { id, ...rest } = item;
    item = rest.parent;
  }
}
function f1() {
  let n: 1 | 2 = 1;
  while (cond()) {
    const { a, ...rest } = { a: n, b: n };
    n = rest.b;
  }
}
function f2() {
  let n: 1 | 2 = 1;
  while (cond()) {
    const [a, ...rest] = [n, n];
    n = rest[0];
  }
}
const sentinel: number = "";
