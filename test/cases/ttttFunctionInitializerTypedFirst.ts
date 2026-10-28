// @strict: true
// A variable initialized by a function expression is typed before its initializer is checked (tsc's
// checkVariableLikeDeclaration asks getTypeOfSymbol first): the function's first check (checkSignatureDeclaration's
// parameter defaults) then runs while the variable is in flight, and a default reading it is circular, TS7022
export {}
const h = (a = h) => 1;
const k = function (b = k) { return 1; };
const ok = (c = 1) => ok;
