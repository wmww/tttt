// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function pipe1<A, B>(f: (a: A) => B): { (a: A): B };
function outer<Q>(q: Q) { return pipe1(<T,>(x: T) => [x, q] as [T, Q]); }
const r: [string, number] = outer(1)("");
const sentinel: number = "";
