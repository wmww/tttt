// @target: es2015
// @noEmit: true
// An anonymous function expression's error span is its first token (tsgo's GetErrorRangeForNode without a name), and
// `yield*`'s TS2488 is at its operand's error span (mutation testing seed 20261103, generatorTypeCheck27)
function* g(): IterableIterator<(x: string) => number> {
    yield * function* () {
        yield (x: string) => x.length;
    };
}
declare function h(cb: number): void;
h(function () {
    return 1;
});
h(async function () {
});
