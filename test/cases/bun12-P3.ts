// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C<T> { p!: { a: T<1> } }
type U = C<1>["p"] | C<2>["p"];
const n: never = null! as U;
const sentinel: number = "";
