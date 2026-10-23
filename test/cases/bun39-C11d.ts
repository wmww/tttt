// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "declaration": true, "isolatedDeclarations": true }, "files": ["node_modules/x/index.ts"] }
// @filename: node_modules/x/index.ts
export const c = [1].map(x => x);
const sentinel: number = "";
