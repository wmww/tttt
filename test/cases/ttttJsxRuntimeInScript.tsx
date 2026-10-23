// A .tsx script (no import or export) still imports its JSX runtime (jsxImportSource): tsgo's fileloader adds it to
// every .tsx file's module names, so JSX.IntrinsicElements comes from solid-js/jsx-runtime
// @jsx: preserve
// @jsxImportSource: solid-js
// @strict: true
// @moduleResolution: bundler
// @module: esnext
// @filename: node_modules/solid-js/package.json
{"name": "solid-js", "version": "1.9.13", "type": "module", "exports": {"./jsx-runtime": {"types": "./types/jsx.d.ts"}}}
// @filename: node_modules/solid-js/types/jsx.d.ts
export namespace JSX {
  interface EventHandler<T, E extends Event> {
    (e: E & {}): void;
  }
  interface BoundEventHandler<T, E extends Event, EHandler extends EventHandler<T, any> = EventHandler<T, E>> {
  }
  type EventHandlerUnion<T, E extends Event, EHandler extends EventHandler<T, any> = EventHandler<T, E>> =
    EHandler | BoundEventHandler<T, E, EHandler>;
  interface HTMLAttributes<T> {
    onClick?: EventHandlerUnion<T, MouseEvent> | undefined;
  }
  interface IntrinsicElements {
    span: HTMLAttributes<HTMLSpanElement>;
  }
}
// @filename: a.tsx
const c = <span onClick={(e) => {}} />;
