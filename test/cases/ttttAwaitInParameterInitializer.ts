// @target: es2022
// @module: esnext
// An `await` in a parameter initializer is TS2524, and TS1308 too where the parameters aren't an await context
// (tsc's checkGrammarAwaitOrAwaitUsing: a function's parameters are in its own await context, not the outer one)
function r2(a = [await 1]) {}
function r3(a = await 1) {}
async function r4(a = await 1) {}
const f1 = (a = await 1) => 0;
const f2 = async (a = await 1) => 0;
async function g() { const f3 = (a = await 1) => 0; function h(b = await 2) {} }
class C { m(a = await 1) {} constructor(b = await 1) {} }

export {};
