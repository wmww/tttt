// @strict: true
// @target: es2020
const enum Kind { a = "resolvedA", b = "resolvedB" }
interface Links { resolvedA?: Map<string, number>; resolvedB?: Map<string, number> }
declare function getLinks(): Links;
declare const empty: Map<string, number>;
declare function more(): boolean;
export function get(kind: Kind): Map<string, number> {
    const links = getLinks();
    if (!links[kind]) {
        let resolved: Map<string, number> | undefined;
        if (more()) {
            links[kind] = resolved;
        }
        links[kind] = resolved || empty;
    }
    return links[kind];
}
interface T { base?: number }
export function base(t: T): number {
    if (!t.base) {
        let result: number | undefined;
        t.base ??= result || 0;
    }
    return t.base;
}
