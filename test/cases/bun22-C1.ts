// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K<T> { constructor(x: T) {} }
class D<T> extends K<T> {}
declare function f<T>(a: typeof D<T>, b: T): T;
declare const s: new (x: string | number) => any;
f(s, "").length;
const sentinel: number = "";
