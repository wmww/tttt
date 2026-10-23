// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export type Bar<A extends string> = A;
type Z4<T> = T extends import("./a").Bar<infer X> ? 1 : never;
const sentinel: number = "";
