// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function i2<T>(x: T, y = i2(x, undefined)) { return x; }
const sentinel: number = "";
