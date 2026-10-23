// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function E<T>(): T;
declare function f<U>(x: U): (y: typeof E<U>) => void;
const v1: 1 = f(1);
const sentinel: number = "";
