// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type F4<X> = X extends [infer A extends F4<A>] ? A : never;
type F6<X> = X extends [infer A extends F6<string>] ? A : never;
type W3<X> = X extends [infer A extends typeof w3] ? A : never; declare const w3: W3<[1]>;
type W6<X> = X extends [infer A extends W7["p"]] ? A : never; interface W7 { p: W6<[1]> }
function fn<X>(x: X extends [infer A extends ReturnType<typeof fn>] ? A : never) { return x; }
class Cl<X> { p!: X extends [infer A extends Cl<A>["p"]] ? A : never; }
const sentinel: number = "";
