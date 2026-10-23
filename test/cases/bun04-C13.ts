// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedParameters": true } }
// @filename: a.ts
type Foo<T> = intrinsic;
const sentinel: number = "";
