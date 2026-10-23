// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
class C3 { [arguments.length]() {} }
const o = { get [arguments.length]() { return 1; } };
const sentinel: number = "";
