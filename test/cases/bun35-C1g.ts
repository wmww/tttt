// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": ["foo"], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import "dep";
const sentinel: number = "";
// @filename: node_modules/@types/foo/index.d.ts
export {};
declare global { const foo: 1; }
// @filename: node_modules/@types/foo/package.json
{ "name": "@types/foo", "version": "1.0.0" }
// @filename: node_modules/dep/index.d.ts
import "foo";
export {};
// @filename: node_modules/dep/node_modules/@types/foo/index.d.ts
export {};
declare global { const foo: 1; }
// @filename: node_modules/dep/node_modules/@types/foo/package.json
{ "name": "@types/foo", "version": "1.0.0" }
