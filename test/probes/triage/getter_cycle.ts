// @strict: true
// Mutually recursive getters through generic calls: tsc reports TS7022 on variantA, variantB and tree and TS7023 on
// child and sibling (variantA: Schema<{ kind: "a"; readonly child: any; }>), tttt resolves the cycle silently with
// different types (variantA any, child Schema<unknown>). zod's real getter schemas (mini/tests/index.test.ts:340) are
// fine under both. See agents/bugs/type_core.md "Circularity"
interface Schema<T> { _t: T }
declare function object<S extends Record<string, Schema<any>>>(shape: S): Schema<{ [K in keyof S]: S[K]["_t"] }>;
declare function optional<T>(s: Schema<T>): Schema<T | undefined>;
declare function literal<L extends string>(l: L): Schema<L>;
declare function union<O extends Schema<any>[]>(options: O): Schema<O[number]["_t"]>;
const variantA = object({
    kind: literal("a"),
    get child() {
        return optional(tree);
    },
});
const variantB = object({
    kind: literal("b"),
    get sibling() {
        return optional(tree);
    },
});
const tree = union([variantA, variantB]);
