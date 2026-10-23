// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type AA<T> = BB<T>[]; type BB<T> = CC<T>[]; type CC<T> = T[];
type AA2<T> = BB2<T>[]; type BB2<T> = CC2<T>[]; type CC2<T> = T[];
declare function f<T>(x: AA<T>): T;
declare let v: AA2<number>;
const r7: string = f(v);
const sentinel: number = "";
