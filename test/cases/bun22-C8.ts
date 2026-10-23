// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function mk<T>() { return class { static #p = 1; static v: T } }
declare function fs<T>(x: ReturnType<typeof mk<T>>): T;
fs(class { static v = 1; static e = 2 }).toFixed();
const sentinel: number = "";
