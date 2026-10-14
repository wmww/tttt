// @allowJs: true
// @checkJs: true
// Within its own type, a function with expandos declared in a file or namespace prints as `typeof f`, a local one as
// the cyclic placeholder `any` (tsc's shouldWriteTypeOfFunctionSymbol)
// @filename: a.ts
function topLevel() {}
topLevel.self = topLevel;
function outer() {
    function local() {}
    local.self = local;
    local.proto = { ctor: local };
    return local;
}
namespace N { function nl() {} nl.self = nl; export const r = nl; }
// @filename: b.js
function jo() { function jl() {} jl.self = jl; return jl; }
