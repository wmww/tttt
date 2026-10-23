// @noEmit: true
// Known failing (agents/bugs/missing_checks.md "for-initializer DisallowIn"): tsgo's DisallowIn context of a for
// statement's initializer reaches into arrow parameters, array literals and nested function bodies (only
// parenthesized expressions, arguments, element accesses, templates, object literal values, conditional branches,
// binding patterns, nested variable statements and statement expressions clear it), so `x in b` there is a syntax error
declare let x: any, b: any;
for (var i = (a = x in b) => 1;;) {}
for (var j = [x in b];;) {}
for (var k = function () { x in b };;) {}
