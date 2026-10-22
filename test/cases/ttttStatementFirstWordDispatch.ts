// @target: esnext
// @noEmit: true
// Statements whose first word decides which statement parsers are tried (parse_statement()'s first-word dispatch):
// labels after trivia, contextual keywords used as identifiers, `await using`, escaped keywords
declare function use(...args: any[]): any;
let type = 1, declare = 2, module = 3, namespace = 4, abstract = 5, global = 6, async = 7;
type = declare + module;
namespace = abstract * global;
async = 1;
outer /* a comment */ : for (const x of [1]) {
    inner
    : while (true) { break outer; }
}
lbl: use(type);
async function f() {
    await using res = { [Symbol.asyncDispose]: async () => {} };
    await use(res);
    await
    use(1);
}
var escaped = 1;
var escaped2 = 2;
let escaped3 = 3;
export { escaped, escaped2 };
