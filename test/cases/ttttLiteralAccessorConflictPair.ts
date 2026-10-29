// An object literal's getter takes its setter's annotation as return type only when the binder put them in one symbol:
// after `x()` conflicted with `set x`, that symbol is both accessors and `get x` conflicts too (tsgo's declareSymbolEx),
// so its `return "0"` isn't checked against number (seed 20261113, duplicateIdentifierChecks num-to-str)
const o8 = {
    set x(value: number) { },
    x() { return 0 },
    get x() { return "0" }
}
const o9 = {
    set x(value: number) { },
    get x() { return "0" }
}
