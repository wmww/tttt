// @strict: true
interface Expr { kind: number; e: 1 }
interface Bindable extends Expr { b: 1 }
interface Call { arguments: readonly Expr[] }
type BCall = Call & { readonly arguments: readonly [Bindable, Expr] };
declare function useB(b: Bindable): void;
function f(node: BCall) {
    useB(node.arguments[0]);
}
interface Decl { kind: number }
interface EnumMember extends Decl { kind: 5; v: number }
declare function getDecl<T extends Decl>(s: string, kind: T["kind"]): T | undefined;
declare function value(m: EnumMember): number;
value(getDecl("x", 5)!);
interface OptA { type: "list"; element: string }
interface OptB { type: "string" }
type Opt = OptA | OptB;
declare function useList(o: OptA): void;
function g(opt: Opt) {
    const t = opt.type;
    if (t === "list") useList(opt);
}
