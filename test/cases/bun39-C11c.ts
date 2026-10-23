// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "rewriteRelativeImportExtensions": true }, "files": ["a.ts", "node_modules/x/index.ts"] }
// @filename: a.ts
import { p } from 'x/index.ts';
p;
const sentinel: number = "";
// @filename: node_modules/x/index.ts
export const p = 1;
