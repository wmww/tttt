// @strict: true
interface Arbitrary<T> { map<U>(f: (t: T) => U): Arbitrary<U>; _t: T }
type LetrecValue<T> = { [K in keyof T]: Arbitrary<T[K]> };
interface LetrecTypedTie<T> {
    <K extends keyof T>(key: K): Arbitrary<T[K]>;
    (key: string): Arbitrary<unknown>;
}
type LetrecTypedBuilder<T> = (tie: LetrecTypedTie<T>) => LetrecValue<T>;
type LetrecLooselyTypedTie = (key: string) => Arbitrary<unknown>;
type LetrecLooselyTypedBuilder<T> = (tie: LetrecLooselyTypedTie) => LetrecValue<T>;
declare function letrec<T>(builder: T extends Record<string, unknown> ? LetrecTypedBuilder<T> : never): LetrecValue<T>;
declare function letrec<T>(builder: LetrecLooselyTypedBuilder<T>): LetrecValue<T>;
declare function constant<T>(t: T): Arbitrary<T>;
declare function oneof<T>(...a: Arbitrary<T>[]): Arbitrary<T>;
declare function decode(s: string): number;
const r = letrec((tie) => ({
  None: constant("None" as const),
  FiberId: oneof(tie("None")) as any as Arbitrary<string>
})).FiberId.map(decode)
