// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o: { m(this: string): void } = { m(this) {} };
const sentinel: number = "";
