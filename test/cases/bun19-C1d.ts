// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Ignore<X> = number;
function f<T>(x: number, y: T extends string ? Ignore<T> : number = x) {}
const sentinel: number = "";
