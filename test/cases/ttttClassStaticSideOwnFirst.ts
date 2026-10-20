// @declaration: true
// @strict: true
// A class's static side lists the statics it declares first, then the inherited ones (tsc's getNamedMembers with the
// class as container), whatever file the base is in: react-three-fiber's ErrorBoundary class expression
// @filename: base.ts
export declare class Base<P> { static contextType?: number; static propTypes?: any; p: P }
// @filename: a.ts
import { Base } from "./base";
export const E = (() => class E extends Base<{ a: 1 }> {
  state = { error: false }
  static g = () => ({ error: true })
})()
export const F = class F extends Base<{ a: 1 }> {
  static g = () => ({ error: true })
}
