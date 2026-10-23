// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function unbox<T>(x: { [K in keyof T]: { v: T[K] } }): T;
function g<Q>() { return unbox({ a: { v: 1 } }); }
type U = ReturnType<typeof g<1>> | ReturnType<typeof g<2>>;
const n: never = null! as U;
const sentinel: number = "";
