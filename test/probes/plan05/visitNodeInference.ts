// @strict: true
interface N { kind: number }
interface E extends N { e: string }
declare function isE(node: N): node is E;
type VisitResult<T extends N | undefined> = T | readonly N[];
type Visitor<TIn extends N = N, TOut extends N | undefined = TIn | undefined> = (node: TIn) => VisitResult<TOut>;
declare function visit<TIn extends N | undefined, TVisited extends N | undefined, TOut extends N>(node: TIn, visitor: Visitor<NonNullable<TIn>, TVisited>, test: (node: N) => node is TOut): TOut | (TIn & undefined) | (TVisited & undefined);
declare function visit<TIn extends N | undefined, TVisited extends N | undefined>(node: TIn, visitor: Visitor<NonNullable<TIn>, TVisited>, test?: (node: N) => boolean): N | (TIn & undefined) | (TVisited & undefined);
declare const m: E | undefined;
declare function visitor(node: N): VisitResult<N>;
const b = visit(m, visitor, isE);
