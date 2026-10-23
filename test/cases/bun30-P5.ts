// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export class T { get a() { return 1 } a() {} }
new T().a = 3;
const sentinel: number = "";
