// @target: es2015
// @strict: true
// @noFallthroughCasesInSwitch: true
// Under parser recovery a node ends with its last token, not the trivia after it, and a clause reaches what recovery
// skipped up to its first statement (tsc's getErrorSpanForNode, node ends). Seed 20261114: fallFromLastCase2
// add-optional (TS7029 at `default?:`), contextualComputedNonBindablePropertyType delete-stmt (TS2464 at `[key in
// keyof` without its `]`), inferTypesWithExtends1 drop-as (TS7039 at a mapped type without its `}`)
declare function use(a: string): void;
function foo2(a: number) {
    switch (a) {
        case 1:
            use("1");
            break;
        default?:
            use("2");
        case 2:
            use("3");
    }
}
declare function forceMatch2<T>(matched: T): void;
forceMatch2({ [key in keyof T]: 1 });
type X18<T> = T extends { [P in keyof T U extends P]: 1; } ? 1 : 0;
