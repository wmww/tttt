// @strict: true
interface A1 { wrapped: string; kind: 1 }
interface A2 { wrapped: string | number; kind: 2 }
function unwrap<T extends A1 | A2>(t: T): T['wrapped'] {
    const w = t.wrapped;
    return t.wrapped;
}
