// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const b: 1 = null! as { get x(this: 1): number; set x(v: string) };
const sentinel: number = "";
