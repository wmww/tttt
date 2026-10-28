// @noEmit: true
// A destructuring assignment's rest element with an initializer is TS1186 and never checked further (tsc's
// checkArrayLiteralDestructuringElementAssignment): no TS2304 for an unknown name in the initializer (mutation testing
// seed 20261104, restElementWithInitializer2)
var x: number[];
[...x = a] = b;
[...(x = c)] = [];
