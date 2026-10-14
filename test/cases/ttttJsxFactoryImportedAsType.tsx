// A JSX tag needs its factory as a value (tsc's markJsxAliasReferenced -> resolveName with isUse): one imported with
// `import type` is TS1361 at each tag name (trpc's next-sse-chat dialog.tsx under `jsx: react`). A fragment isn't an
// expression to report at, nor is a namespaced tag name
// @jsx: react
// @module: preserve
// @filename: react.d.ts
declare namespace React { function createElement(...a: any[]): any; }
export = React;
declare global { namespace JSX { interface IntrinsicElements { [x: string]: any } } }
// @filename: main.tsx
import type React from './react';
declare namespace Ns { const Comp: any }
export const a = <div />;
export const b = <><span /></>;
export const c = <Ns.Comp>text</Ns.Comp>;
