// @noEmit: true
// A for-in or for-of declaration's initializer (TS1189/TS1190) is checked against the variable's type (tsc's
// checkVariableLikeDeclaration), except a for-in's at the symbol's first declaration; a secondary one's is (bun 83-R01c,
// another file's `for (var i = [x in b];;)`)
for (var x = "s" of [1]) {}
for (let y = 1 in {}) {}
declare var z: string;
for (var z = 1 in {}) {}
for (var w = (a) => 1 in {}) {}
var w: string;
for (var w = (a) => 1 in {}) {}
