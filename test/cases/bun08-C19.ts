// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["x.d.ts", "a.ts"] }
// @filename: a.ts
const sentinel: number = "";
// @filename: comp.tsx
export const c = 1;
// @filename: x.d.ts
export {};
declare module "./comp" { interface A {} }
