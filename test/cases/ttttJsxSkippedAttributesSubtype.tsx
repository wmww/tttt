// The applicability check that skips context-sensitive attributes relates their regular type, still an object literal
// to tsc (getRegularTypeOfObjectLiteral keeps ObjectFlags.ObjectLiteral): in the subtype pass it needn't have
// IntrinsicAttributes' optional `key` (requireOptionalProperties). So the second overload passes that check and its
// State types `selection` for good, before the first overload is chosen in the assignable pass (tanstack-table's
// `<table.Subscribe source={...} selector={(selection) => ...}>`, the source any)
// @jsx: react
// @strict: true
// @filename: a.tsx
/// <reference path="/.lib/react16.d.ts" />
import * as React from 'react';
interface State { a: number; "1": string }
declare const Subscribe: {
  <TS, TSel>(props: { source: { get(): TS }; selector: (state: TS) => TSel; children: ((state: TSel) => string) | string }): JSX.Element
  <TSel>(props: { selector: (state: State) => TSel; children: ((state: TSel) => string) | string }): JSX.Element
}
declare const anyValue: any
export const y = <Subscribe source={anyValue} selector={(selection) => Boolean(selection['1'])} children={(selected) => String(selected)} />
