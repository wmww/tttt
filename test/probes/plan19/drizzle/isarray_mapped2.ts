// @strict: true
type V<T extends { ins: object }> = { [K in keyof T["ins"]]: T["ins"][K] | null } & {};
type W<T extends { ins: object }> = { [K in keyof T["ins"]]: T["ins"][K] | null };
function f<T extends { ins: object }>(v: V<T>, w: W<T>) {
    const a: V<T> | V<T>[] = [v];
    const b: W<T> | W<T>[] = [w];
    const c: V<T> = [v];
    const d: W<T> = [w];
}
