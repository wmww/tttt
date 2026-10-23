// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "rootDir": "src", "types": [], "lib": ["es2022"] }, "files": ["src/a.ts", "node_modules/b/node_modules/x/index.ts"] }
// @filename: node_modules/b/index.d.ts
import "x";
// @filename: node_modules/b/node_modules/x/index.ts
export const x = 1;
// @filename: node_modules/b/node_modules/x/package.json
{"name":"x","version":"1.0.0","types":"index.ts"}
// @filename: node_modules/b/package.json
{"name":"b","version":"1.0.0","types":"index.d.ts"}
// @filename: node_modules/x/index.ts
export const x = 1;
// @filename: node_modules/x/package.json
{"name":"x","version":"1.0.0","types":"index.ts"}
// @filename: src/a.ts
import "x";
import "b";
export const sentinel: number = "";
