//// [test/cases/ttttReturnTypeUniqueUnresolved.ts] ////

//// [a.ts]
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


//// [a.js]
export function body(node, b) {
    if (b)
        return finish(node, "x");
    return finish(node, "y");
}
export const arrow = async (node, b) => {
    if (b)
        return finish(node, "x");
    return finish(node, "y");
};


//// [a.d.ts]
import type * as N from "./missing";
export type Undone<T> = Omit<T, "type">;
export declare function body(node: Undone<N.EnumBody>, b: boolean): N.EnumBody;
export declare const arrow: (node: Undone<N.EnumBody>, b: boolean) => Promise<N.EnumBody>;
