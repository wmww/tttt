// A function component needing more arguments than the factory gives (TS6229): a union tag type's union signatures
// are checked too, and an overloaded one fails every overload (TS2769, the last overload's TS6229)
// @jsx: react
// @strict: true
// @filename: a.tsx
declare namespace React {
    function createElement(type: (props: any) => any, props?: any, ...children: any[]): any;
    function createElement(type: string, props?: any, ...children: any[]): any;
}
declare namespace JSX {
    interface Element {}
    interface IntrinsicElements { div: any }
}
declare function A(props: {}, ctx: {}, extra: number): JSX.Element;
declare function B(props: {}, ctx: {}, extra: string): JSX.Element;
declare const AB: typeof A | typeof B;
const x = <AB />;
declare function O(props: { a: number }, ctx: {}, extra: number): JSX.Element;
declare function O(props: { b: string }, ctx: {}, extra: number): JSX.Element;
const y = <O a={1} />;
const z = <A />;
