// @strict: true
// A renamed binding element of a bodiless signature is implicitly any only once something asks for the
// parameter's type (tsc's getTypeOfSymbol of the parameter): the call to g, not f, which nothing calls
declare function f({y1: string}): void;
declare function g({y2: string}): void;
g({ y2: 1 });
type F = typeof f;
