// @strict: true
// @jsx: preserve
// A union target whose one naked type parameter's sibling members matched every source: the whole source is still
// inferred to the naked one, less directly (tsc's inferToMultipleTypes falls through to its last loop). framer-motion's
// `motion.create(Button)`: TagName is then its constraint (string), not its default ("div"), and the component keeps
// Button's props (abacusai-bot/apps/web: 24 spurious TS2322/TS2551 on the props of motion components)
declare namespace React { type ComponentType<P> = (props: P) => any }
interface DOMMotionComponents { div: (props: { d: 1 }) => any; span: (props: { s: 1 }) => any }
type MotionComponent<T, P> = T extends keyof DOMMotionComponents ? DOMMotionComponents[T] : React.ComponentType<P & { m?: 1 }>;
declare function create<Props, TagName extends keyof DOMMotionComponents | string = "div">(Component: TagName | string | React.ComponentType<Props>): MotionComponent<TagName, Props>;
declare function Button(props: { variant?: "a" | "b"; size?: string }): any;
const MB = create(Button);
declare const t: [typeof MB];

declare function Button(props: { variant?: "a" }): any;
declare const ButtonC: (props: { variant?: "a" }) => any;
declare function c1<P, T extends string = "div">(c: T | ((props: P) => any)): [T, P];
const x1: 1 = c1(Button);
const x2: 1 = c1(ButtonC);
declare function c2<P, T = "div">(c: T | ((props: P) => any)): [T, P];
const x3: 1 = c2(Button);
declare function c3<P, T = "div">(c: T | { f: (props: P) => any }): [T, P];
const x4: 1 = c3({ f: Button });
declare function c4<P, T = "div">(c: T | (() => P)): [T, P];
const x5: 1 = c4(() => 1);
