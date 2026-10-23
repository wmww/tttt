// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type N26 = 1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26;
type U = { a: 1, z: typeof v } | { a: 2, z: string };
declare function take(u: U): number;
declare function take(u: unknown): string;
declare const src: { a: N26, z: string };
const v = take(src);
const sentinel: number = "";
