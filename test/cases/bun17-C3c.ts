// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Foo<T> { m<U>(cb: T, u: U): void }
interface Bar<T> { m<U>(cb: T, u: U): void }
declare let f: Foo<(x: string) => void>;
f as Bar<(x: number) => void>;
const sentinel: number = "";
