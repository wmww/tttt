// @strict: true
// A homomorphic mapped type over a union takes a key's optionality from the union's property, which a member with an
// applicable index signature instead of the property joins without making it required (tsc's
// createUnionOrIntersectionProperty, CheckFlags.WritePartial): `Omit<Record<string, any> | { class?: string }, "x">`
// keeps `class` optional. openscience's frontend: solid-js's ComponentProps of a Kobalte polymorphic component, where
// a computed `[DIFFS_TAG_NAME]` augmentation of an unresolved import gives JSX.IntrinsicElements an index signature,
// fell to `Record<string, unknown>` and made `split.class` unknown (spurious TS2464s, TS2322s)
type U = Record<string, any> | Record<string, unknown> | { class?: string };
export const o: Omit<U, "x"> = {};
export const p: Pick<U, "class"> = {};
export const m: { [K in keyof U]: U[K] } = {};
type U2 = { readonly [k: string]: number } | { class?: number };
export const u2: Pick<U2, "class"> = {};
u2.class = 1;
type U3 = { [k: string]: number } | { class: number };
export const u3: Pick<U3, "class"> = {};
interface IE { div: { class?: string }; [k: string]: { class?: string } }
type Component<P extends Record<string, any> = {}> = (props: P) => void;
type VC = keyof IE | Component<any>;
type CP<T extends VC> = T extends Component<infer P> ? P : T extends keyof IE ? IE[T] : Record<string, unknown>;
type ElementOf<T> = T extends HTMLElement ? T : any;
declare const K: <T extends VC = "div">(props: Omit<CP<T>, "x"> & { ref?: ElementOf<T> }) => void;
export const r: CP<typeof K> = { ref: 1 };
