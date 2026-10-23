// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const MB: new () => Readonly<{ a: () => void }>;
class D extends MB { a() {} }
const sentinel: number = "";
