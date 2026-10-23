// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export namespace N10 { export import A = M10.B; export const z = A; }
export namespace M10 { export import B = N10.z; }
const sentinel: number = "";
