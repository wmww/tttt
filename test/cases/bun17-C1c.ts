// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Box<T> { v: T }
type IA<T> = Box<IB<T>>; type IB<T> = Box<T>;
type JA<T> = Box<JB<T>>; type JB<T> = Box<T>;
declare function h<T>(x: IA<T>): T;
declare let j: JA<number>;
h(j).toFixed();
const sentinel: number = "";
