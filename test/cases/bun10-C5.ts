// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export namespace N9 { export class K extends M9.A {} }
export namespace M9 { export import A = N9.K; }
const sentinel: number = "";
