enum SyntaxKind { A, B, C, FirstX = A, LastX = B }
interface Node { readonly kind: SyntaxKind; }
interface NA extends Node { readonly kind: SyntaxKind.A; a: string }
interface NB extends Node { readonly kind: SyntaxKind.B; b: number }
type Has = NA | NB;
type Fn<T extends Node> = (node: T, x: number) => T;
type Table = { [TNode in Has as TNode["kind"]]: Fn<TNode>; };
const table: Table = {
    [SyntaxKind.A]: function visitA(node, x) { return node; },
    [SyntaxKind.B]: function visitB(node, x) { return node; },
};
