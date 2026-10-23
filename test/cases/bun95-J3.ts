// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"]}}
// @filename: a.ts
declare function run(a: string, p: number | undefined): number | undefined;
declare function ap<A extends any[], R>(f: (...args: A) => R, args: A): R;
export function f(n: number) {
  let acc: number | undefined = undefined;
  for (let i = 0; i < n; i++) {
    acc = ap(run, ["x", acc]);
  }
  return acc;
}
export function g(n: number) {
  let acc: number | undefined = undefined;
  for (let i = 0; i < n; i++) {
    acc = run.apply(null, ["x", acc]);
  }
  return acc;
}
const sentinel: number = "";
