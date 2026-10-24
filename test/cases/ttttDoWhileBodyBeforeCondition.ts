// @strict: true
// A do-while's body is checked before its condition (tsc's checkDoStatement): the read of `c` at the loop head is in
// flight when `r`'s initializer asks for `c` again, which answers the loop's partial type instead of walking from the
// condition's assignment back to `r` (TS7022 on `r` otherwise)
interface Item { parent(): Item | Root; }
interface Root { kind: number; host: Item; }
declare function seen(i: Item | undefined): boolean;
export function f(start: Item): boolean {
    let c: Item | undefined = start;
    do {
        if (seen(c)) return true;
        const r = c.parent();
        c = "kind" in r ? r.host : undefined;
    } while (c !== undefined);
    return false;
}
