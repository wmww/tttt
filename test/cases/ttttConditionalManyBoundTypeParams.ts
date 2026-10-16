// @strict: true
// A conditional type whose check type is an object type mentioning no free type variable resolves, however many type
// parameters its members bind on the way: a call signature with 17 type parameters (in an instance of a type literal
// whose free type parameters aren't known syntactically: `typeof a`) made the walk looking for free ones give up, and
// the conditional type stayed deferred (arktype's `{ ...type }` through entryOf<o>: its callback parameters stayed
// `entryOf<...>[0]`, TS2769; and that project checked 4x slower)
type MergeParser<$> = {
    <A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q>(a: A, q: Q): [typeof a, $];
};
interface Parser<$ = {}> {
    merge: MergeParser<$>;
    raw(def: unknown): $;
}
declare const parser: Parser<{}>;
const spread = { ...parser };
type IsArray<O> = O extends readonly unknown[] ? 1 : 2;
const a: IsArray<typeof spread> = 3;
type entryOf<o> = { [k in keyof o]-?: [k, o[k]] }[o extends readonly unknown[] ? keyof o & number : keyof o];
declare const e: entryOf<typeof spread>;
const k: { a: 1 } = e[0];
