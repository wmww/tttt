// @module: esnext
// A local export's string name without `as` is allowed (tsc's checkExportSpecifier checks the name with
// allowStringLiteral and skips the rest); only a string before `as` is TS1003 (seed 20261111)
const x = 1;
export { "x" };
export { x as "y z" };
export { "a b" as c };
