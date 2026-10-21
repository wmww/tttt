// An unresolved `import x = A.B` exported or used as a value references A as a value (tsc's markLinkedReferences ->
// markIdentifierAliasReferenced): a namespace only is TS2708, not TS2304, and nothing at all is said from an ambient
// context, where markLinkedReferences returns (mutation testing, reExportAliasMakesInstantiated's `.test1X`)
// @filename: ambient.ts
declare namespace mod1 {
  type test1 = string;
  export { test1 };
}
declare namespace mod2 {
  import test1 = mod1.test1X;
  export { test1 };
}
const test2 = mod2;
export {}
// @filename: plain.ts
namespace mod3 {
  export type test1 = string;
}
namespace mod4 {
  import test1 = mod3.test1X;
  export { test1 };
}
export {}
