// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum E { A = 1, B = 2 }
declare function f1<T extends E>(x: `${T}`): T;
f1("1.0");
const sentinel: number = "";
