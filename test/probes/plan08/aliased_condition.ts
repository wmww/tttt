// @strict: true
declare const r: { file: string } | undefined;
declare function ok(): boolean;
function f() {
    const sf = r && ok() && r.file;
    if (sf) {
        r.file;
    }
    const isStr = typeof sf === "string";
    if (isStr) {
        sf.length;
    }
}
function g(x: string | number) {
    const isString = typeof x === "string";
    if (isString) {
        x.length;
    }
}
