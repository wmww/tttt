// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C42 { declare m(): void; m() {} }
class D4 { export m(): void; m() {} }
const sentinel: number = "";
