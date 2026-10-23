// @strict: true
// An element access on an unknown object with a generic key is TS18046 and then the error type, like any other key
// (tsc's checkNonNullExpression comes first): no TS2345 for passing it on, no TS2571 for calling it (mulmoclaude)
declare function take(s: string): void;
export function f<K extends string>(item: unknown, k: K, o: { item: unknown }) {
    take(item[k]);
    take(item["x"]);
    take(item.x);
    take(o.item[k]);
    take(o.item.y);
    const e = item[k];
    take(e);
    item[k]();
    item();
    new item();
}
