// @strict: true
type Get<T> = T extends { data: infer D } ? D : never;
declare class SQL<T = unknown> {
    t: T;
    mapWith<TD extends { data: unknown }>(d: TD): SQL<Get<TD>>;
}
function f<T, TD extends { data: unknown }>(s: SQL<T>, t: T, u: TD) {
    const a = s as SQL<Get<TD>>;
    const b = t as Get<TD>;
    const c = t as TD;
    const d = u as T;
}
