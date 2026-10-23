// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function h1<T extends ReturnType<typeof h1>>() { return 1 as any as T; }
const sentinel: number = "";
