// @strict: true
import * as runtime from "nonexistent";
type E = runtime.JSX.ElementType;
type ElementType = any extends E ? never : E;
type FC<P> = ElementType extends never ? (props: P) => null : never;
const f: FC<number> = (props) => null;
type K = keyof runtime.JSX.IntrinsicElements;
const k: K = "x";
const k2: Extract<K, string> = "x";
type M = { [Key in Extract<K, string>]?: (p: number) => void };
const m: M = { a(p) {} };
export {};
