// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { p: this extends { z: 1 } ? 1 : 2 }
interface A extends I { z: 1 }
interface B extends I { y: 1 }
function f<T extends I>(t: T) { return t.p; }
declare let ab: A | B;
const bad: 2 = f(ab);
const sentinel: number = "";
