// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "strictNullChecks": false, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Show<T> = { show: T };
declare function show<T>(x: T): Show<T>;
function f<TU extends unknown>(x: TU) { if (x instanceof Object) { const b: number = show(x); } }
const sentinel: number = "";
