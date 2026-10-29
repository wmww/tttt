// @strict: true
// An export specifier in a namespace's block (TS1194) puts its name in scope there (bun cases 01-C11, 33-C11a): tsc's
// resolveNameHelper skips a pure export specifier alias only in a source file or an ambient module declaration
export {};
namespace N { const a = 1; export { a as b }; b; }
const v = 1;
namespace M { export { v as o }; o; }
N.b;
declare namespace D { const c: number; export { c as d }; type T = typeof d; }
namespace P { export { q }; q; }
namespace Q { function f() { return g; } export { f as g }; }
