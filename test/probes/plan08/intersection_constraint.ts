// @strict: true
interface Node { kind: number }
interface NodeArray<T extends Node> extends ReadonlyArray<T> { hasTrailingComma: boolean }
declare function createNodeArray<T extends Node>(elements: T[]): NodeArray<T>;
export function parseList<T extends Node | undefined>(parse: () => T): NodeArray<NonNullable<T>> | undefined {
    const list: NonNullable<T>[] = [];
    const r = parse();
    if (!r) return undefined;
    list.push(r);
    return createNodeArray(list);
}
