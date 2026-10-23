// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const a1 = [{ p: { get g() { return a1; } } }, { p: { b: 1 } }];
const sentinel: number = "";
