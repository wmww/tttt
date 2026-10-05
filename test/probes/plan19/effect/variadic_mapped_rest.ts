// @strict: true
// @target: es2022
interface Arbitrary<T> { _t: T }
declare const a: Arbitrary<number>
declare const b: Arbitrary<string[]>
declare function p3<Ts extends unknown[]>(x: { [K in keyof Ts]: Arbitrary<Ts[K]> }): Ts;
export const r3 = p3([a, b])
declare function p4<Ts extends unknown[]>(...x: { [K in keyof Ts]: Arbitrary<Ts[K]> }): Ts;
export const r4 = p4(a, b)
declare function p5<Ts extends unknown[]>(...x: [...{ [K in keyof Ts]: Arbitrary<Ts[K]> }]): Ts;
export const r5 = p5(a, b)
declare function p6<Ts extends [unknown, ...unknown[]]>(...x: [...{ [K in keyof Ts]: Arbitrary<Ts[K]> }]): Ts;
export const r6 = p6(a, b)
