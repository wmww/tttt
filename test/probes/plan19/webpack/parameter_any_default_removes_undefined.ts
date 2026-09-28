// @strict: true
declare const d: any;
declare const e: unknown;
function f(s: { a: number } | undefined = d) {
    const g = () => s.a;
    return s.a;
}
function h(s: { a: number } | undefined = e as any) {
    return s.a;
}
