// @module: commonjs
// @target: es6
// A spread specifier of a dynamic import (TS1325) is checked as its element type: (string | number)[] is TS7036
var a: (string | number)[] = ["./", 0];
var p1 = import(...a);
var b = ["./b"];
var p2 = import(...b);
