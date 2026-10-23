// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const { a, ...rest } = { a: 1 };
declare let r3: typeof rest & string;
const e2: number = r3;
const sentinel: number = "";
