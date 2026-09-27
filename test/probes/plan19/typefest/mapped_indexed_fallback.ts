// @strict: true
type UnionToIntersection<U> = (U extends any ? (k: U) => void : never) extends ((k: infer I) => void) ? I : never;
type Specs = { a: { x?: string }, b: { y?: number } };
type Args<K extends keyof Specs> = UnionToIntersection<Specs[K]>;
type PM = { [K in keyof Specs]: { args: Args<K> } };
interface RM extends ReadonlyMap<string, PM[keyof PM]> {
  forEach(action: <K extends keyof PM>(value: PM[K], key: K) => void): void;
}
