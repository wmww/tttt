// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @filename: a.js
// tsc: a is any[]. Checking the context-sensitive arrow instantiates its contextual signature with the fixing mapper;
// instantiating the JSDoc function type `() => void` of s fixes R (isTypeParameterPossiblyReferenced: walking up from a
// JSDoc type never meets the @template tag, so R counts as referenced), before R has candidates: the JS default any.
// With `s: string` a is { running: boolean; }[]. webpack MultiCompiler.js _runGraph (TS18048 x2, TS2345).
/**
 * @template R
 * @param {(i: number, s: () => void) => R} setup
 * @param {(r: R) => void} run
 * @returns {R[]}
 */
function runGraph(setup, run) { return []; }
const a = runGraph((i, s) => ({ running: false }), (r) => {});
