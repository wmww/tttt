// @strict: true
// @noImplicitAny: true
interface Ctx { onNode: (hint: number, node: string) => string; readonly x: number }
let f = (hint: number, node: string) => node;
const ctx: Ctx = {
    x: 1,
    get onNode() { return f; },
    set onNode(value) { f = value; },
};
declare function getTime(): Date | undefined;
declare const fallback: Date;
function g(cb: (e: string, t?: Date) => void) {}
g((e, t) => {
    t ||= getTime() || fallback;
    t.getTime();
});
