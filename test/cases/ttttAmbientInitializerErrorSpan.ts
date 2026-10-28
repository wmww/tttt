// @strict: true
// TS1039 goes where tsgo's GetErrorRangeForNode puts an initializer: an anonymous function expression's assigned name
// (GetNameOfDeclaration), a class expression's `class`, a parenthesized expression whole
declare let a = function () {};
declare var b = class { m() {} };
declare let c = (function () {});
declare let d = () => 1;
class D3 { declare p = function () {}; declare q = (class {}); }
