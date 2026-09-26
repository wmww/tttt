// @strict: true
// vitest: interface Assertion<T> extends VitestAssertion<Chai.Assertion, T>, JestAssertion<T>,
// where VitestAssertion is `{ [K in keyof A]: ... } & ((type: string) => Assertion)`
type M = { a: 1 } & { y: 1 };
interface I extends M { x: number }
declare const i: I;
i.a;
i.y;

interface ChaiAssertion { not: ChaiAssertion; to: ChaiAssertion; }
type VitestAssertion<A, T> = {
    [K in keyof A]: A[K] extends ChaiAssertion ? Assertion<T> : A[K];
} & ((type: string, message?: string) => Assertion);
interface Assertion<T = any> extends VitestAssertion<ChaiAssertion, T> {
    toBe: <E>(expected: E) => void;
}
declare const a: Assertion<number>;
a.not.toBe(1);
a("x").to;
