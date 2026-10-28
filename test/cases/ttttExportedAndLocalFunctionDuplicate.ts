// An exported function and a local function of the same name in one block are one local symbol in tsc (the binder
// declares the exported one locally too, ExportValue): two implementations, TS2393 on both
// (checkFunctionOrMethodDeclaration's checkFunctionOrConstructorSymbol of the local symbol; missing_checks.md)
export function g() {}
function g() {}
namespace N {
    function h() {}
    export function h() {}
}
