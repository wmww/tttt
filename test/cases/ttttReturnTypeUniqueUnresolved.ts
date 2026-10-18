// @declaration: true
// @strict: true
// The types a function returns are collected once each (tsc's AppendIfUnique in checkAndAggregateReturnExpressionTypes):
// the same unresolved name's error type returned twice is that type, printed by its name, not the union of its two
// copies, which is any (babel-parser's `flowEnumBody(...): N.EnumBody` with unbuilt workspace dependencies).
// @filename: a.ts
import type * as N from "./missing";
export type Undone<T> = Omit<T, "type">;
declare function finish<T>(node: Undone<T>, type: string): T;
export function body(node: Undone<N.EnumBody>, b: boolean) {
    if (b) return finish(node, "x");
    return finish(node, "y");
}
export const arrow = async (node: Undone<N.EnumBody>, b: boolean) => {
    if (b) return finish(node, "x");
    return finish(node, "y");
};
