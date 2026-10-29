// TS1200 (a line break before `=>`) is a grammar error, tsc's checkGrammarArrowFunction: a file with parse errors
// doesn't get it (seed 20261115, arrowFunctionErrorSpan delete-stmt)
declare function f(a: () => number): void;
f(() => {

f(()
    => { });
