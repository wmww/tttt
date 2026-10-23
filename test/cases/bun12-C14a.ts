// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function mk<A>(a: A): A[];
function g<T>() { return null! as typeof mk<number>; }
const n: never = g<string>();
const sentinel: number = "";
