interface E4<A, E, R> { _a: () => A; _e: () => E; _r: () => R }
declare const e4: E4<number, string, "svc">
declare const p4: <I>(tag: I) => <A, E, R>(self: E4<A, E, R>) => E4<A, E, Exclude<R, I>>
declare function pipe<A, B>(a: A, ab: (_: A) => B): B
const x5: E4<number, string, never> = pipe(e4, p4("svc"))
declare const p5: <I>(tag: I) => <R>(self: E4<number, string, R>) => E4<number, string, Exclude<R, I>>
const x6: E4<number, string, never> = pipe(e4, p5("svc"))
declare const p6: (tag: string) => <A, E, R>(self: E4<A, E, R>) => E4<A, E, Exclude<R, string>>
const x7: E4<number, string, never> = pipe(e4, p6("svc"))
const y5 = pipe(e4, p4("svc"))
export {}
