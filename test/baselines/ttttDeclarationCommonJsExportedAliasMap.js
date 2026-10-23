//// [test/cases/ttttDeclarationCommonJsExportedAliasMap.ts] ////

//// [enums.js]
Object.defineProperty(exports, "__esModule", { value: true });
var Kind;
(function (Kind) {
    Kind["A"] = "a";
})(Kind = exports.Kind || (exports.Kind = {}));




//// [enums.d.ts]
export declare var __esModule: boolean;
declare const _exported: {};
export { _exported as Kind };
//# sourceMappingURL=enums.d.ts.map