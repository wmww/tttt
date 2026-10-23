// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const obj = { a: 1 };
declare const OB: new () => typeof obj;
class D extends OB { get a() { return 1 } }
const sentinel: number = "";
