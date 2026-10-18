// @strict: true
// @noTypesAndSymbols: true
// @noEmit: true
// drizzle-orm's QueryPromise subclasses (clidiff on drizzle): a conditional type instantiated with every outer type
// parameter mapped to itself is the one written (tsgo seeds root.instantiations with it), so the class's members,
// instantiated for `this`, keep the distributed R of `R[]`: the inherited fin() is identical to the interface's base's
// and the error is about execute()
declare abstract class QP<T> {
    fin(): Promise<T>;
    abstract execute(): Promise<T>;
}
export interface Del<R> extends QP<R extends undefined ? number : R[]> {}
export class Del<R> extends QP<R extends undefined ? number : R[]> {
    override execute: () => Promise<string> = undefined!;
}
