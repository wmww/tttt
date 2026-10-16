// `OmitThisParameter<typeof f>` where f's `this` is an alias of an intersection that has a property of that very type:
// no TS2502 (a regression of flattening the lone aliased contravariant candidate at ThisParameterType's `infer U`,
// @testing-library/user-event's setup.d.ts `Instance`)
// @strict: true
// @lib: es2020,dom
declare function dispatchEvent(this: Instance, target: Element): boolean;
declare const api: { click(this: Instance): void };
type Instance = typeof api & {
  config: number;
  dispatchEvent: OmitThisParameter<typeof dispatchEvent>;
};
export declare const i: Instance;
