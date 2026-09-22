// @strict: true
type E = { kind: "class"; ci: number } | { kind: "el"; next: { ci: number } };
declare let tp: E | undefined;
function f() {
    switch (tp?.kind) {
        case "class": return tp.ci;
        case "el": return tp.next.ci;
        default: return tp;
    }
}
