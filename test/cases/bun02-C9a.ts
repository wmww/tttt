// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A19 { static get constructor() { return 1 } }
class A25 { static *constructor() {} }
const sentinel: number = "";
