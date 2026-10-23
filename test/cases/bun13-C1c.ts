// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Q = keyof M<C>;
type M<T> = { [K in keyof T]: T[K] };
declare const B: new () => M<C>;
class C extends B { m() {} }
const sentinel: number = "";
