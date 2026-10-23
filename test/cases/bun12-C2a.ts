// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T>(x: T) { return ({ a = x }) => a; }
f(1)({ a: 1 });
const sentinel: number = "";
