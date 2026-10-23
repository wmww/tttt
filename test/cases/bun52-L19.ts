// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Unk = unknown; type Nev = never;
function f1<T extends string & number & T>() {}
function f2<T extends T | Unk>() {}
function f3<T extends T & Nev>() {}
function f4<T extends U | Unk, U extends T>() {}
function f5<T extends { [K in T & Nev]: 1 }>() {}
function f6<T extends T & (string & number)>() {}
const sentinel: number = "";
