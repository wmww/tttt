// @strict: true
// semver.ts TS2678, not freshness: tsc's isTypeEqualityComparableTo accepts any nullable case type
function parse(operator: string) {
    switch (operator) {
        case "<":
        case undefined:
        case null:
            break;
    }
}
