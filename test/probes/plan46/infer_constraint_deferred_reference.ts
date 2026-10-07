// @strict: true
interface Internal<P, Q, E, R, X> { p: P; q: Q; e: E; r: R; x: X }
type Comp<P> = (props: P) => void;
interface Props<Q, E, R, X> { q: Q; e: E; r: R; x: X }
type EPC<
    Q extends Record<string, number>,
    E extends Record<string, EP<any, any> | undefined>,
    R extends {} = {},
    X extends {} | null = {},
> = Comp<Props<Q, E, R, X>>;
type EP<C, P extends {} = {}> = Internal<
    P,
    C extends EPC<infer Q, any, any, any> ? Q : never,
    C extends EPC<any, infer E, any, any> ? E : never,
    C extends EPC<any, any, infer R, any> ? R : never,
    C extends EPC<any, any, any, infer X> ? X : never
>;
type N<T> = T extends EPC<infer A, infer B, infer C, infer D> ? [A, B, C, D] : 0;
const x: N<any> = 1 as unknown as string;
