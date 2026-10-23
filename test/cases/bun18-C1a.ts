// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type S = (p: string) => string;
type T = (p: string | number) => string | number;
interface Foo<X> { m<U>(a: X, b: U): void; m<U>(a: X, b: U, c: U): void }
interface Bar { m<U>(a: T, b: U): void; m<U>(a: T, b: U, c: U): void }
declare let foo: Foo<S>;
let bar: Bar = foo;
const sentinel: number = "";
