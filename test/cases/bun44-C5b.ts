// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type A = { kind: "a"; v: number };
type B = { kind: "b"; v: string };
function f<T extends A | B>({ kind, v }: T & { x: 1 }) {
  if (kind === "a") v.toFixed();
  else v.length;
}
const sentinel: number = "";
