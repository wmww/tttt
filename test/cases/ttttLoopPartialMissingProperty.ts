// @strictNullChecks: true
// @noTypesAndSymbols: true
// An access first checked while a loop's flow analysis is unfinished is reported with the loop's partial type
// ('None', the first round's), once: tsc's reportNonexistentProperty doesn't report the final 'Optional<r>' again.
// (Mutation of typeGuardsAsAssertions)

declare let cond: boolean;

type Optional<a> = Some<a> | None;
interface None { readonly none: string; }
interface Some<a> { readonly some: a; }
declare const none: None;
declare function isSome<a>(value: Optional<a>): value is Some<a>;

function someFrom<a>(some: a) {
    return { some };
}

function fn<r>(makeSome: () => r): void {
    let result: Optional<r> = none;
    while (cond) {
        result = someFrom(isSome(null) ? result.some : makeSome());
    }
}

function fn2<r>(makeSome: () => r): void {
    let result: Optional<r> = none;
    while (cond) {
        result = { some: isSome(null) ? result.some : makeSome() };
    }
}

// A call of a single non-generic signature isn't evaluated by the loop (tsc's getQuickTypeOfExpression): the final type
declare function sf(x: any): Some<any>;
function fn3<r>(makeSome: () => r): void {
    let result: Optional<r> = none;
    while (cond) {
        result = sf(isSome(null) ? result.some : makeSome());
    }
}
