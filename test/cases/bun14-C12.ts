// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f0<T>() {
  type A = { a: 1 }[{ [K in keyof T]: 1 }];
}
const sentinel: number = "";
