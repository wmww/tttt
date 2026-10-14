// A parameter of a signature without a body whose annotation is `typeof` itself (the parameter shadows the outer name)
// is TS2502 at the whole parameter (tsc's checkParameter -> getTypeOfSymbol, reportCircularityError at its
// valueDeclaration), in type literals, interfaces and function types too (bumpp's `(currentVersion, semver: typeof
// semver) => ...` in soybean-admin)
declare const semver: { a: number };
interface O {
  f?: (cur: string, semver: typeof semver) => void;
  g(semver: typeof semver): void;
}
type F = (x: typeof x) => void;
declare function h(y: typeof y): void;
function k(z: typeof z) {}
type C = new (w: typeof w) => void;
var u: typeof u;
