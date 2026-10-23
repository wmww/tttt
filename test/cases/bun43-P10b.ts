// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type M<T> = T extends [infer U?] ? U : never;
const sentinel: number = "";
