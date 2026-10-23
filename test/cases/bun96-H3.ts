// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export type { type A } from 123;
export { B } from 123 with { type: "json" };
export { C } from 123 with { type: 1 };
export type { D } from 123 with { "resolution-mode": "x" };
function f() {
  export { E } from 123;
}
{
  export * from 123;
  export * as F from 123;
}
const sentinel: number = "";
// @filename: b.ts
declare export { A } from 123;
