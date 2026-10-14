// @target: es2015
// Type parameters that start at a modifier, `in` or `const` but have no name: TS1359/TS1003, a missing name (`(Missing)`)
type A<in extends X> = {};
type B<const> = {};
type C<in in T> = { a: T };
function f<in>() {}
interface I<in in> {}
