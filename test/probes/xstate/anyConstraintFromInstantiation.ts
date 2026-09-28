// A type parameter whose constraint is any by instantiation (`<TType extends E>` of Ref<any>) is related through it like
// tsc (any is related to everything): xstate's Actor<any> is an AnyActorRef. A declared `extends any` is `extends
// unknown` (tsc's getConstraintFromTypeParameter), whose failure is not elaborated
interface Ref<E> {
  on: <TType extends E>(type: TType) => void;
}
declare const f: <TType extends string>(type: TType) => void;
const g: Ref<any> = { on: f };
function h<T extends any>(t: T) { const s: string = t; }
