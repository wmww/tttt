// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "rootDir": "src" }, "files": ["src/a.ts", "other/c"] }
// @filename: other/c.ts
const c = 1;
// @filename: src/a.ts
const sentinel: number = "";
