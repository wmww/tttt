// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const cond: boolean;
((p = { f: 1, h: z => {} }) => {})(cond ? { f: 1, h: (z: number) => {} } : { g: 1, h: (z: string) => {} });
const sentinel: number = "";
