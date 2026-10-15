// @declaration: true
// @strict: true
// `u === "a"` narrows a union of primitives that has `string` (or "a") to the regular "a" (tsc's isUniformUnionType
// path of narrowTypeByEquality): it doesn't widen in a return type or a `let`. A plain `string` narrows by filtering
// to the fresh "a", which does
declare const s: string;
declare const u: string | undefined;
declare const sn: string | number;
export const v = (() => {
    const env = u?.trim();
    if (env === 'lmdb' || env === 'sqlite') {
        return env;
    }
    return undefined;
})();
export function f() {
    if (u === 'a') return u;
    return 1;
}
export function g() {
    if (s === 'a') return s;
    return 1;
}
export function h() {
    if (sn === 1) { let q = sn; return q; }
    return "x";
}
