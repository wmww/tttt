// @strict: true
// @exactOptionalPropertyTypes: true
// The operand of `x!` is checked without the check mode (tsc's checkNonNullAssertion calls checkExpression): even
// inside a call's inference, a generic element access with a union constraint in a context that isn't generic is its
// constraint (getNarrowableTypeForReference). effect's Struct.renameKeys through buildStruct
// (The types component also shows a pre-existing difference: `m[k]` in a generic call's argument prints unsubstituted)
declare function buildStruct<S extends object, F extends (k: keyof S, v: S[keyof S]) => [string | number, unknown] | undefined>(source: S, f: F): any;
function rename<S extends object, const M extends { readonly [K in keyof S]?: string | number }>(self: S, mapping: M) {
    return buildStruct(self, (k, v) => [mapping[k]!, v]);
}
