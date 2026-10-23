// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function h5<T extends ReturnType<typeof h5>>(x: T) { return x; }
const sentinel: number = "";
