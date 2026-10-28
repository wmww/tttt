// @strict: true
// @target: es2022
// @module: es2022,commonjs
// @erasableSyntaxOnly: true
// `declare export = 1` is TS1120, and ambient all the same (tsgo's parser's NodeFlagsAmbient): checkExportAssignment's
// TS2714 on a non-entity expression, no TS1294, and TS1203 only in an ES module declaration (bun 03-C17a, 03-C17b)
declare export = 1;
