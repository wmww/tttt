function g<S, K extends keyof S, J extends string>(t: number) {
    const x1: Pick<S, K> = { a: t };
    const x2: { [P in J]: number } = { a: t };
    const x3: { [P in keyof S]: number } = { a: t };
    const x4: Partial<S> = { a: t };
    const x5: { [P in K]: number } = { a: t };
    const x6: { [P in keyof S as `x${P & string}`]: number } = { a: t };
    const x7: Record<J, number> = { a: t };
    const x8: { [P in keyof S | "a"]: number } = { a: t };
}
function h<S extends { b: number }, J extends "a" | "b">(t: number) {
    const x1: Pick<S, keyof S> = { a: t, b: 1 };
    const x2: { [P in J]: number } = { a: t, c: 1 };
    const x3: Partial<S> = { b: 1, c: t };
}
