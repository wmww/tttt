// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "declaration": true, "isolatedDeclarations": true } }
// @filename: a.ts
export const o3 = { get y(): number { return 1; }, set y(this: any) {} };
export const o2 = { get x(): number { return 1; }, set x(this: any, v: number) {} };
const sentinel: number = "";
