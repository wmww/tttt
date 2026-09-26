// @strict: true
// react-query corpus: `const { onSuccess, ...rest } = mutationConfig || {}` where mutationConfig is a weak type
// (all-optional). The union `Opts<..> | {}` is subtype-reduced; both are subtypes of each other unless the rule
// "nothing non-empty is a (strict) subtype of a fresh empty object literal" applies, so which one survives
// depends on type creation order in tttt.
const early = {};
interface Opts<T> { onSuccess?: (x: T) => void; retry?: number }
declare const c: Opts<string> | undefined;
const { onSuccess, ...rest } = c || {};
const v = c || {};
