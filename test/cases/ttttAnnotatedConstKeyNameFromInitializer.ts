// @strict: true
// @target: esnext
// An annotated const whose annotation names no property names a reference by its initializer's type: tsc's
// tryGetNameFromEntityNameExpression tries the annotation, then the initializer, so `this[N.key] = ...` assigns
// `[N.key]` (no TS2564; mutation testing, typeGuardNarrowsIndexedAccessOfKnownProperty7 with `const key: never`)
const k: never = Symbol() as never;
const j: never = Symbol();
class D { [j]: string; constructor() { this[j] = "hello"; } }
namespace N { export const key: never = Symbol(); }
class C { [N.key]: string; constructor() { this[N.key] = "hello"; } }
class E { [N.key]: string; constructor() { } }
