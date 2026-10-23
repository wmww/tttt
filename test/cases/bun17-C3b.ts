// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Foo<T> { m<U>(cb: T, u: U): void; m(x: number): void }
interface Bar<T> { m<U>(cb: T, u: U): void; m(x: number): void }
declare let f: Foo<(x: string) => void>;
declare let g: Bar<(x: string | number) => void>;
f = g;
const sentinel: number = "";
