//// [test/cases/ttttIsolatedSetterReturnFallback.ts] ////

//// [ttttIsolatedSetterReturnFallback.ts]
// An object literal the pseudo checker can't type (an accessor pair without annotations) is printed from the checker's type:
// an accessor pair whose types differ prints as both signatures, and the setter's return type is serialized from
// the pseudo checker's inference of its body (serializeReturnTypeForSignature): an inference fallback, TS9009 at
// the setter's parameter. A pair with one type is a property, nothing more. Seed 20261119,
// isolatedDeclarationErrorsObjects ann-number-string
export let o = {
    get bad() { return 0 },
    set bad(value) { },
    get differ(): number { return 0 },
    set differ(value: string) { },
    get same(): number { return 0 },
    set same(value: number) { },
}
export let fine = {
    get differ(): number { return 0 },
    set differ(value: string) { },
}


//// [ttttIsolatedSetterReturnFallback.js]
// An object literal the pseudo checker can't type (an accessor pair without annotations) is printed from the checker's type:
// an accessor pair whose types differ prints as both signatures, and the setter's return type is serialized from
// the pseudo checker's inference of its body (serializeReturnTypeForSignature): an inference fallback, TS9009 at
// the setter's parameter. A pair with one type is a property, nothing more. Seed 20261119,
// isolatedDeclarationErrorsObjects ann-number-string
export let o = {
    get bad() { return 0; },
    set bad(value) { },
    get differ() { return 0; },
    set differ(value) { },
    get same() { return 0; },
    set same(value) { },
};
export let fine = {
    get differ() { return 0; },
    set differ(value) { },
};
