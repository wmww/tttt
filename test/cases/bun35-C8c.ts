// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": ["foo"], "preserveSymlinks": true, "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
/// <reference types="foo/" />
const sentinel: number = "";
// @filename: node_modules/@types/foo/index.d.ts
declare const foo: 1;
