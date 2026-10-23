// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type F3<X> = X extends (infer A extends F3<A>) ? A : never;
type F8<X> = X extends [infer A extends F8<A>[]] ? A : never;
type H4<X> = X extends [infer A extends keyof H4<A>] ? A : never;
const sentinel: number = "";
