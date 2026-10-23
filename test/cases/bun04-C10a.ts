// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedParameters": true } }
// @filename: a.ts
const sentinel: number = "";
// @filename: g1.ts
interface GI { m<T>(): void; }
// @filename: g2.ts
interface GI { m<U>(x: number): void; }
