// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: node_modules/@types/foo/index.d.ts
declare const fooGlobal: 1;
// @filename: src/a.ts
/// <reference types="./foo" />
const a: 1 = fooGlobal;
const sentinel: number = "";
