// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function mk<A>(a: A): A[];
function g<T>() { return mk<number>; }
type U = ReturnType<typeof g<1>> | ReturnType<typeof g<2>>;
const n: never = null! as U;
const sentinel: number = "";
