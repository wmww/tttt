// @noEmit: true
// tsgo's DisallowIn context of a for statement's initializer reaches into arrow parameters, array literals and nested
// function and class bodies; only parenthesized expressions, element accesses, template spans, computed names, the
// expressions of if/while/for/return/throw/switch/case/with, arguments, object literal values, conditional true
// branches, binding patterns, property and enum initializers and nested variable declaration lists clear it, so
// `x in b` elsewhere there is a syntax error (agents/bugs/missing_checks.md "for-initializer DisallowIn")
declare let x: any, b: any, c: any, d: any;
for (var i = (a = x in b) => 1;;) {}
for (var j = [x in b];;) {}
for (var k = function () { x in b };;) {}
for (var l = c ? x in b : d;;) {}
for (var m = f(x in b);;) {}
for (var n = { a: x in b };;) {}
for (var o = function () { var q = x in b; if (x in b) {} return x in b; };;) {}
for (var p = class { y = x in b; [x in b] = 1; m() { x in b } };;) {}
for (var r = function () { for (var s = 0; x in b; x in b) { x in b } };;) {}
for (var t = `${x in b}`;;) {}
for (var u = function ({ y = x in b }, [z = x in b]) {};;) {}
for (var v = function () { enum E { A = x in b, B = A } };;) {}
for (var w = (x in b);;) {}
function f(..._: any[]) {}
