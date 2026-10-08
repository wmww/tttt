# Two missing-diagnostic quirks from the source sweep (plan 52 drain, ss-d round 2)

## `import * as X` of an `export =` module under esModuleInterop is a *clone* of the target (docmost, -TS2322)
tsc's resolveESModuleSymbol: a namespace import (or `import()`) of an `export =` whose type has call/construct
signatures (or a `default`) under esModuleInterop resolves to `cloneTypeAsModuleType(symbol, ...)`: a new transient
symbol with the same flags and declarations. Its value type is the synthetic module type (no signatures: `new X()` is
TS2351, tttt has that), and its *type* meaning is the clone's own declared interface type, a distinct type from the
original interface with its own `this` type, printed `import("<path>")`. So the original and the clone don't relate
when the interface returns `this` somewhere. Repro (`/tmp/ss-d-probes/p14` shape):

```ts
// node_modules/jz/index.d.ts
interface JSZip { file(path: string): JSZip.JSZipObject | null; file(path: string, data: string): this;
  folder(name: string): JSZip | null; new (): this; (): JSZip; prototype: JSZip; }
declare namespace JSZip { interface JSZipObject { name: string } }
declare var JSZip: JSZip;
export = JSZip;
// a.ts, module commonjs (esModuleInterop is on by default in 6.0), strict false
import * as JSZip from 'jz';
export function f(zip: JSZip) {
  const stack: { folder: JSZip }[] = [{ folder: zip }];
  const { folder } = stack.pop();
  stack.push({ folder: folder.folder("x") });   // tsc: TS2322 Type 'import(".../jz/index")' is not assignable to type 'JSZip'
}
```
Fixing it means a second declared type for the same declarations, keyed by the import (tttt keys declared types by
symbol): plan-sized for one repo; only worth it if more show up.

## Property access on a union member `E & null` with E constrained to the error type (react-navigation, -TS2339)
`T extends Unresolved` narrowed by `=== undefined` is `T & ({} | null)`; tsc reports TS2339 on it ("does not exist on
type 'T & null'"). Probing tsc (`function f<E extends Unresolved>(a: E & {}, b: E & null, c: E & { c: 3 },
d: (E & {}) | (E & null))`): only `d.x` is an error, `a.x`, `b.x`, `c.x` alone are not. So a lone intersection with
an error-constrained member is any-like but a union of them isn't (getUnionOrIntersectionProperty skips members whose
apparent type is the error type, and the union's property needs every member). tttt's check_access treats any union
member with a type parameter as opaque. Needs reading tsc's getPropertyOfType for unions of intersections first; the
lone-intersection case must stay silent.
