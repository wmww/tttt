// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type S = (p: string | number) => void;
type T = (p: string) => void;
class C<X> { m<U>(a: X, b: U): void {} }
class E extends C<T> { m<U>(a: S, b: U): void {} }
const sentinel: number = "";
