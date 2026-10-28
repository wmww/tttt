// @strict: true
// @target: es2022
// A `yield` outside a generator body but inside a generator (a type literal's computed name, a class field) is TS1163
// and still checked against that generator (tsc's checkYieldExpression, GetContainingFunction): TS7057 without a
// return type annotation (bun 33-P6a); one in a type isn't among the generator's yields (forEachYieldExpression skips
// type nodes), one in a class field is
function* g() { let x: { [yield 1]: 1 }; }
function* h() { class C { p = yield 2; } }
function* k(): Generator<number, void, string> { let x: { [yield "a"]: 1 }; }
