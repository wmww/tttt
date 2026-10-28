// @allowJS: true
// @checkJs: true
// @noEmit: true
// @strict: true
// A `@param` under an object tag nests only while its name's left is an open object tag's (tsc's
// parseChildParameterOrPropertyTag): `opts5[].whatX.bad` closes both `what` and `opts5`, every later tag is top level
// (TS8032, the names printed as entity names, without `[]`) and `unnest` isn't a member (mutation testing,
// jsdocParamTagTypeLiteral member-typo)
// @Filename: 0.js
/**
 * @param {object[]} opts5 - Let's test out some multiple nesting levels
 * @param {string} opts5[].help - (This one is just normal)
 * @param {object} opts5[].what - Look at us go! Here's the first nest!
 * @param {string} opts5[].what.a - (Another normal one)
 * @param {Object[]} opts5[].whatX.bad - Now we're nesting inside a nested type
 * @param {string} opts5[].what.bad[].idea - I don't think you can get back out of this level...
 * @param {boolean} opts5[].what.bad[].oh - Oh ... that's how you do it.
 * @param {number} opts5[].unnest - Here we are almost all the way back at the beginning.
 */
function foo5(opts5) {
    opts5[0].what.bad[0].idea;
    opts5[0].unnest;
}

foo5([{ help: "help", what: { a: 'a', bad: [{ idea: 'idea', oh: false }] }, unnest: 1 }]);
