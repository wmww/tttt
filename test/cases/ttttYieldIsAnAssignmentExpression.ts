// @target: es2015
// A yield expression is a whole assignment expression (tsc's parseAssignmentExpressionOrHigher returns
// parseYieldExpression's), and takes an operand only on its own line: nothing continues it
function* g1() { yield
[]; }
function* g2() { yield
/a/.nope; }
function* g3() { yield
*[]; }
function* g4(): any { yield ? 1 : 2; }
function* g5(): any { const x = yield, y = 1; }
