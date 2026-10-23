// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": ["foo"], "lib": ["es2022"] }, "files": ["a.ts"] }
// @filename: a.ts
import "y";
const sentinel: number = "";
// @filename: node_modules/@types/foo/index.d.ts
export {};
declare global { const fooGlobal: 1; }
// @filename: node_modules/@types/foo/package.json
{"name":"@types/foo","version":"1.0.0","types":"index.d.ts"}
// @filename: node_modules/y/index.d.ts
import "foo";
export declare const y: 1;
// @filename: node_modules/y/node_modules/@types/foo/index.d.ts
export {};
declare global { const fooGlobal: 1; }
// @filename: node_modules/y/node_modules/@types/foo/package.json
{"name":"@types/foo","version":"1.0.0","types":"index.d.ts"}
// @filename: node_modules/y/package.json
{"name":"y","version":"1.0.0","types":"index.d.ts"}
