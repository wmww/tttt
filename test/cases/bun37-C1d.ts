// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "types": [], "lib": ["es2022"], "rootDir": "src", "outDir": "out" }, "files": ["a.ts"], "include": ["*.ts"] }
// @filename: a.ts
const sentinel: number = "";
