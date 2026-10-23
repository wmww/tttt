// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const TB: new () => [() => void];
class D extends TB { 0() {} }
const sentinel: number = "";
