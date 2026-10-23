// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type AB = { kind: "a", a: 1, f(): void } | { kind: "b", b: 2 };
function f1<T extends AB>(x: T) {
  if (x.kind === "a") {
    x.a;
    (x).a;
    (x)["a"];
    (x).f();
  }
}
const sentinel: number = "";
