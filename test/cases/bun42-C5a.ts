// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts", "root.TS"] }
// @filename: a.ts
const sentinel: number = "";
// @filename: root.TS
const rootGlobal: 0 = 1;
