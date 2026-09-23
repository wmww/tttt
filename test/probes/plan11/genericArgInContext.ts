// @strict: true
// parenthesizerRules.ts / tsbuildPublic.ts: a generic function passed where a non-generic signature is expected is
// instantiated in the context of that signature (instantiateSignatureInContextOf), not erased to its constraint
interface Node { n: number; }
interface NodeArray<T extends Node> extends ReadonlyArray<T> { hasTrailingComma: boolean; }
interface Expression extends Node { e: string; }
declare function isNodeArray<T extends Node>(array: readonly T[]): array is NodeArray<T>;
declare function cast<TOut extends TIn, TIn = any>(value: TIn | undefined, test: (value: TIn) => value is TOut): TOut;
declare function identity<T>(x: T): T;
const rules: { f(elements: readonly Expression[]): NodeArray<Expression> } = { f: nodes => cast(nodes, isNodeArray) };
function g<P>(p: P) {
    function withProgram<U>(action: (program: P) => U | undefined): U | undefined { return action(p); }
    const r: { get: () => P | undefined } = { get: () => withProgram(identity) };
    return r;
}
const arr = [1, 2, 3].map(identity);
