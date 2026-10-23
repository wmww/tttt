// `yield`/`await` outside a generator/async function are operators when an operand follows on the same line, also
// after a comment (hacks.md: the operand lookahead peeked at characters, a comment meant "no operand")
function f() { var x = yield /*c*/ 1; }
function g() { var y = yield 1; }
function h() { var z = await /*c*/ 1; }
function k() { var w = await 1; }
function m() { var v = yield /*
*/ 1; }
