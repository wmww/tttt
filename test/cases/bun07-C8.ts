// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
interface C<T> { v: T }
class C<const T> { constructor(x: T) { } }
const p1: number = new C([1, 2]);
const sentinel: number = "";
