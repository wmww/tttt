// origin: foldkit/repos/effect packages/effect (source sweep), http-api/HttpApiEndpoint.ts:1151 (makeProto's return-type inference)
// Known failing: Q's candidate from the declared return, `P extends Fields ? Struct<P> : P`, is checked against
// getTypeWithThisArgument(Top, candidate); tsgo passes it, tttt fails the this-argument comparison of P's constraint
// @strict: true
interface Bottom<T, Rebuild extends Top> {
    readonly Type: T;
    readonly Rebuild: Rebuild;
    rebuild(): this["Rebuild"];
}
interface Top extends Bottom<unknown, Top> {}
type Fields = { readonly [k: string]: Top };
interface Struct<F extends Fields> extends Bottom<F, Struct<F>> { readonly fields: F }
interface Box<Q extends Top> { q: Q }
declare function mk<Q extends Top>(): Box<Q>;
export function g<P extends Top>(): Box<P extends Fields ? Struct<P> : P> { return mk(); }
