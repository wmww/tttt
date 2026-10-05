// @jsx: preserve
// @strict: true
// tsc: no errors (the attributes are checked against overload B, not typed by A first)
declare namespace JSX { interface Element {} interface IntrinsicElements {} }
interface Ev<T> { target: T; prevent(): void }
interface A { type: "a"; onBlur?: ((e: Ev<number>) => void) | undefined }
interface B { type: "b"; onBlur?: ((e: Ev<string>) => void) | undefined; children?: any }
declare function C(props: A): JSX.Element;
declare function C(props: B): JSX.Element;
const x = <C type="b" onBlur={e => e.prevent()}>code</C>;
const y = <C type="b" onBlur={e => e.prevent()} />;
declare function f(props: A): void;
declare function f(props: B): void;
f({ type: "b", onBlur: e => e.prevent() });
