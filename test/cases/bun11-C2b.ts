// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type B6<T = `${B6 & string}`> = { x: T };
const sentinel: number = "";
