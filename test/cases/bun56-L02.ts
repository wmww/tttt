// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type X<T> = T extends [infer U \u0065xtends string] ? U : never;
type Z<T> = T extends infer U \u0065xtends string ? U : never;
