// Errors without a node of their own (TS2589, TS2590) inside a deferred check (a function body, a JSX element, an
// expression) are reported at the deferred node, like tsc's checkDeferredNode sets currentNode, so a `// @ts-ignore`
// covers them. tiptap's MarkViewContent (`<Tag {...rest} ref=...>` with a generic Tag, whose `ref` props intersect every
// intrinsic element's): tttt printed a location-less TS2590 (fixed in 16b4bb4b). Without the @ts-ignore, tsgo's TS2322
// elaboration lists other missing properties than tttt's (contentEditable... against accessKey...).
// @jsx: react
// @strict: true
// @filename: a.tsx
/// <reference path="/.lib/react16.d.ts" />
import * as React from 'react';
export type ContentProps<T extends keyof JSX.IntrinsicElements = 'span'> = { as?: T } & Omit<React.ComponentProps<T>, 'as'>;
export const Content = <T extends keyof JSX.IntrinsicElements = 'span'>(props: ContentProps<T>) => {
    const { as: Tag = 'span', ...rest } = props;
    const r = (element: HTMLElement | null) => {};
    return (
        // @ts-ignore
        <Tag {...rest} ref={r} />
    );
};
