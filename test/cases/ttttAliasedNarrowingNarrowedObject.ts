// @strict: true
// A condition aliased in a const narrows `node.name` when `name` is readonly in the type `node` has there: tsc's
// isConstantReference reads the symbol the access resolved to, on `node`'s narrowed type (the declared Node has no
// `name`). openclaw's `const ok = ts.isObjectBindingPattern(node.name) && ...; if (ok) use(node.name)` after
// `ts.isVariableDeclaration(node)`
interface Node { kind: number }
interface Ident extends Node { kind: 1; text: string }
interface ObjPat extends Node { kind: 2; elements: string[] }
interface ArrPat extends Node { kind: 3; items: number[] }
type BindingName = Ident | ObjPat | ArrPat;
interface VarDecl extends Node { readonly kind: 4; readonly name: BindingName; readonly initializer?: Node }
interface MutDecl extends Node { kind: 5; name: BindingName }
declare function isVarDecl(n: Node): n is VarDecl;
declare function isMutDecl(n: Node): n is MutDecl;
declare function isIdent(n: Node): n is Ident;
declare function isObj(n: Node): n is ObjPat;
declare function useObj(p: ObjPat): void;
declare function other(n: Node): boolean;
function visit(node: Node) {
    if (isVarDecl(node) && !isIdent(node.name)) {
        const ok = node.initializer && isObj(node.name) && other(node.initializer);
        if (ok) {
            useObj(node.name);
        }
    }
    if (isVarDecl(node)) {
        const ok = isObj(node.name);
        if (ok) useObj(node.name);
    }
    if (isMutDecl(node)) {
        const ok = isObj(node.name);
        if (ok) useObj(node.name);
    }
}
