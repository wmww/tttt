//// [test/cases/ttttDeclarationReusedNameExportListed.ts] ////

//// [package.json]
{"name":"redux","types":"index.d.ts"}

//// [index.d.ts]
type Action<T extends string = string> = { type: T };
interface UnknownAction extends Action { [k: string]: unknown }
interface Dispatch<A extends Action = UnknownAction> { <T extends A>(action: T): T }
export { Action, UnknownAction, Dispatch };

//// [wrap.ts]
import type { Dispatch } from 'redux'
export function wrapMapToPropsConstant(getConstant: (dispatch: Dispatch) => { dispatch?: Dispatch }) {
  return function initConstantSelector(dispatch: Dispatch) {
    const constant = getConstant(dispatch)
    function constantSelector() { return constant }
    constantSelector.dependsOnOwnProps = false
    return constantSelector
  }
}

//// [probe.ts]
import { wrapMapToPropsConstant } from './wrap'
export const a = wrapMapToPropsConstant(() => ({}))


//// [wrap.js]
export function wrapMapToPropsConstant(getConstant) {
    return function initConstantSelector(dispatch) {
        const constant = getConstant(dispatch);
        function constantSelector() { return constant; }
        constantSelector.dependsOnOwnProps = false;
        return constantSelector;
    };
}
//// [probe.js]
import { wrapMapToPropsConstant } from './wrap';
export const a = wrapMapToPropsConstant(() => ({}));


//// [wrap.d.ts]
import type { Dispatch } from 'redux';
export declare function wrapMapToPropsConstant(getConstant: (dispatch: Dispatch) => {
    dispatch?: Dispatch;
}): (dispatch: Dispatch) => {
    (): {
        dispatch?: Dispatch;
    };
    dependsOnOwnProps: boolean;
};
//// [probe.d.ts]
export declare const a: (dispatch: import("redux").Dispatch) => {
    (): {
        dispatch?: import("redux").Dispatch;
    };
    dependsOnOwnProps: boolean;
};
