// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": ["foo"], "lib": ["es2022"] }, "files": ["a.ts", "__inferred type names__.ts"] }
// @filename: __inferred type names__.ts
declare const c: 1;
// @filename: a.ts
const f: 0 = foo;
const sentinel: number = "";
// @filename: node_modules/@types/foo/index.d.ts
declare const foo: 1;
