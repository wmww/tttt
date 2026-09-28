// A conditional type against an intersection it isn't related to member by member is still related through its
// distributive constraint, as a whole (tsc's structuredTypeRelatedToWorker goes on for an instantiable source; only a
// conditional *target* rules the distributive constraint out). xstate's Actor.on: listeners.add(handler.bind(undefined))
// @strict: true
type Emitted<L> = L extends { e: infer E } ? E : never;
function f<L extends { e: any }, TType extends Emitted<L>['type'] | '*'>(x: Emitted<L>) {
  const a: TType extends '*' ? unknown : { type: TType } = x;
  const b: Emitted<L> & (TType extends '*' ? unknown : { type: TType }) = x;
}
function g<L extends { e: { type: string } }, TType extends Emitted<L>['type'] | '*'>(x: Emitted<L>) {
  const a: TType extends '*' ? unknown : { type: TType } = x;
}
