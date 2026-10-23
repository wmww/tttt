// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K { constructor(x: string | number) {} }
class D extends K {}
declare const s: new (x: string) => D;
const t: typeof D = s;
const sentinel: number = "";
