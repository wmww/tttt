// @strict: true
interface Node { kind: number }
interface Expression extends Node { e: 1 }
interface Block extends Node { b: 1 }
type ConciseBody = Expression | Block;
type VisitResult<T extends Node | undefined> = T | readonly Node[];
type Visitor<TIn extends Node = Node, TOut extends Node | undefined = TIn | undefined> = (node: TIn) => VisitResult<TOut>;
declare function isConciseBody(node: Node): node is ConciseBody;
interface NodeVisitor {
    <TIn extends Node | undefined, TVisited extends Node | undefined, TOut extends Node>(
        node: TIn,
        visitor: Visitor<NonNullable<TIn>, TVisited>,
        test: (node: Node) => node is TOut,
    ): TOut | (TIn & undefined) | (TVisited & undefined);
    <TIn extends Node | undefined, TVisited extends Node | undefined>(
        node: TIn,
        visitor: Visitor<NonNullable<TIn>, TVisited>,
        test?: (node: Node) => boolean,
    ): Node | (TIn & undefined) | (TVisited & undefined);
}
declare const nodeVisitor: NodeVisitor;
function f(node: ConciseBody | undefined, visitor: Visitor) {
    const updated = nodeVisitor(node, visitor, isConciseBody);
}
function g<TIn extends Node | undefined, TVisited extends Node | undefined, TOut extends Node>(
        node: TIn,
        visitor: Visitor<NonNullable<TIn>, TVisited>,
        test: (node: Node) => node is TOut,
    ): TOut | (TIn & undefined) | (TVisited & undefined) { return null!; }
function h(node: ConciseBody | undefined, visitor: Visitor) {
    const updated = g(node, visitor, isConciseBody);
}
