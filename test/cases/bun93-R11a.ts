// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function id<U>(u: U): U;
class C<T> { w = ({ m() { return 1; }, id }).id<5>; }
let w: C<string>["w"] | C<boolean>["w"] = 1;
function g<T>(a = ({ m() { return 1; }, id }).id<6>) { return a; }
let a: ReturnType<typeof g<string>> | ReturnType<typeof g<boolean>> = 1;
const h = <T,>() => ({ m() { return 1; }, id }).id<7>;
let b: ReturnType<typeof h<string>> | ReturnType<typeof h<boolean>> = 1;
// with a return type annotation the method does not count: one type in both tools
class D<T> { w = ({ m(): number { return 1; }, id }).id<8>; }
let d: D<string>["w"] | D<boolean>["w"] = 1;
const sentinel: number = "";
