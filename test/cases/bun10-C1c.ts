// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function i3<T>(x: T, y = i3(x, 1 as any)) { return y; }
const sentinel: number = "";
