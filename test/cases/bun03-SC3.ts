// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
const x = 1; type T = 1;
export = x;
export { T as Y };
export const Y = 1;
// @filename: z.ts
const sentinel: number = "";
export {};
