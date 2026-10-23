// @allowJs: true
// @checkJs: true
// @noEmit: true

// A JSDoc-typed `module.exports = v` is checked against its `@type` (tsc's checkAssignmentOperator against the type of
// `module.exports`, which getWidenedTypeForAssignmentDeclaration takes from the typed declaration)
// @filename: mod.js
/** @type {{ status: 'done', m(n: number): void }} */
module.exports = {
    status: "doneX",
    m(n) { }
}
// @filename: ok.js
/** @type {{ status: 'done' }} */
module.exports = { status: "done" };
// @filename: untyped.js
module.exports = { a: 1 };
module.exports.b = 2;
