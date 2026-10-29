// A parameter list that meets a `,` where a parameter should be (TS1138) gives up when a list around it wants the
// `,` (tsc's abortParsingListOrMoveToNextToken): in an array literal `function(null, s)` ends at `null`, `s` is the
// next element. At statement level nothing wants it and the list skips it. Seed 20261114, contextualTyping null-arg
var a = [function(null, s) { return s; }];
function f(, s) { return s; }
var b = [(function(x, , y) { return y; })];
