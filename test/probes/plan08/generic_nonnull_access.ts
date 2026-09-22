// @strict: true
interface Ctx { flags: number }
function f<T extends Ctx | undefined>(c: T) {
    return c && c.flags;
}
function g<S extends { name: string } | undefined>(s: S) {
    return s ? s.name : "";
}
