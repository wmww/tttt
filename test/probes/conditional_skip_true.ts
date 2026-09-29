// A conditional target whose true branch can't be picked (not even the permissive instantiation, the type variables
// the wildcard, is assignable) is related to by its false branch alone: tsc's skipTrue. `[wildcard]` relates to
// `[never]`, so IsNotNever<RKeys<wildcard>> is false. xstate's createActor(logic) with a generic logic
// tsc: no errors
type IsNotNever<T> = [T] extends [never] ? false : true;
type ConditionalRequired<T, Condition extends boolean> = Condition extends true ? Required<T> : T;
interface Logic { input: unknown }
type InputFrom<L> = L extends { input: infer I } ? I : never;
type RKeys<L extends Logic> = undefined extends InputFrom<L> ? never : 'input';
declare function create<L extends Logic>(l: L, ...[options]: ConditionalRequired<[options?: { a?: 1 }], IsNotNever<RKeys<L>>>): void;
function f<T extends Logic>(t: T) {
    create(t);
    const x: ConditionalRequired<[options?: { a?: 1 }], IsNotNever<RKeys<T>>> = [<[]>[]][0];
}
