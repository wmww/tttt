// `N.C` where the N found as a namespace (an inner, non-instantiated one) has no member C, but N's value (the outer
// namespace) does: TS2749 for the whole name (tsc's resolveEntityName: canSuggestTypeof, tryGetQualifiedNameAsValue),
// not TS2694. @types/react's `React.Component` once a `declare module "react" { namespace React {...} }` augmentation
// merged a namespace React into React (apollo-client/docs)
namespace N { export class C {} export const v = 1; export namespace M { export class D {} } }
namespace O { export namespace N { export interface Q {} } }
namespace O {
  interface J extends N.C {}
  type U = N.C;
  type W = N.M.D;
  class L implements N.C {}
  type Missing = N.Nope;
  type Q = N.Q;
}
