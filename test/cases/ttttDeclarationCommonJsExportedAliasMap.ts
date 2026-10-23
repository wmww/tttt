// @allowJs: true
// @declaration: true
// @declarationMap: true
// @emitDeclarationOnly: true
// @module: commonjs
// @outDir: out
// @noTypesAndSymbols: true
// A CommonJS export written as `declare const _exported: T; export { _exported as X };` maps its exported name to the
// assignment's property name (tsgo's transformCommonJSExportWorker reuses that node): golevelup-nestjs's compiled enums.
// (Symbols baseline off: `exports.Kind`'s Decl column is 25 in tttt, 27 in tsgo; agents/triage.md)
// @filename: enums.js
Object.defineProperty(exports, "__esModule", { value: true });
var Kind;
(function (Kind) {
    Kind["A"] = "a";
})(Kind = exports.Kind || (exports.Kind = {}));
