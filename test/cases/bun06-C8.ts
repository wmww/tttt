// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function apply<A, B>(f: (a: A) => B, a: A): B;
declare const args: [<T>(x: T) => T[], number];
apply(...args)[0].toFixed();
const sentinel: number = "";
