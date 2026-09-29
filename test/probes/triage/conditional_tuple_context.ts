// @strict: true
type IsNotNever<T> = [T] extends [never] ? false : true;
type ConditionalRequired<T, Condition extends boolean> = Condition extends true ? Required<T> : T;
interface Logic { input: unknown }
type InputFrom<L> = L extends { input: infer I } ? I : never;
type RKeys<L extends Logic> = undefined extends InputFrom<L> ? never : 'input';
function f<T extends Logic>(t: T) {
    const x: ConditionalRequired<[options?: { a?: 1 }], IsNotNever<RKeys<T>>> = [];
    const y: T extends string ? [number?] : [string?] = [];
}
