// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
const b1: (this: { q: 1 }) => void = function (this) {};
const o1: { m(this: { q: 1 }): void } = { m(this) {} };
const sentinel: number = "";
