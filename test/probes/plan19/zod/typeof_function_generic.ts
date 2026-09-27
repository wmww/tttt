// @strict: true
interface Box { _input: any }
function f<Inner extends Box>(d: Inner["_input"] | (() => string)) {
    const r = typeof d === "function" ? d : () => d;
    const g: () => string = r;
    return g;
}
function h<T>(d: T | (() => string)) {
    const r = typeof d === "function" ? d : 1;
    return r;
}
