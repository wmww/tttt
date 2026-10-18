// @declaration: true
// @strict: true
// @module: esnext
// @moduleResolution: bundler
// A reused annotation's name that isn't accessible where it's printed is named anew (tsc's serializeTypeName) through
// any chain that reaches it: a module-local interface exported by an export list is `import("redux").Dispatch`, its
// type arguments as written (none), not printed from its type with the default filled in

// @filename: node_modules/redux/package.json
{"name":"redux","types":"index.d.ts"}

// @filename: node_modules/redux/index.d.ts
type Action<T extends string = string> = { type: T };
interface UnknownAction extends Action { [k: string]: unknown }
interface Dispatch<A extends Action = UnknownAction> { <T extends A>(action: T): T }
export { Action, UnknownAction, Dispatch };

// @filename: wrap.ts
import type { Dispatch } from 'redux'
export function wrapMapToPropsConstant(getConstant: (dispatch: Dispatch) => { dispatch?: Dispatch }) {
  return function initConstantSelector(dispatch: Dispatch) {
    const constant = getConstant(dispatch)
    function constantSelector() { return constant }
    constantSelector.dependsOnOwnProps = false
    return constantSelector
  }
}

// @filename: probe.ts
import { wrapMapToPropsConstant } from './wrap'
export const a = wrapMapToPropsConstant(() => ({}))
