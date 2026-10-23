// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "k";
const j = "j";
type T1 = { get k(); set [k](v: string) };
type T2 = { set [k](v: string); get k() };
type T3 = { set k(v); get [k](): string };
type T4 = { get [k](): string; set k(v) };
function f(p: { get k(); set [k](v: string) }) {}
// The members are resolved before the type literal is checked, or by an earlier late-bound member.
let before: T5["k"];
type T5 = { get k(); set [k](v: string) };
type T6 = { get k(); set [k](v: string); get j(); set [j](v: string) };
// `checkIndexConstraints` resolves the members of these before they are checked.
interface I1 { get k(); set [k](v: string) }
declare class C1 { get k(); set [k](v: string) }
const sentinel: number = "";
