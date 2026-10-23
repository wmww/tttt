// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type P4<T extends Record<string, string>> = { [K in keyof T]: T[K] } extends { [K in keyof T]: (infer U) } ? Uppercase<U> : never;
const sentinel: number = "";
