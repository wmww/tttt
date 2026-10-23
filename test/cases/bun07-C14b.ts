// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
const a3: (this: { q: 1 }) => void = (this) => {};
const sentinel: number = "";
