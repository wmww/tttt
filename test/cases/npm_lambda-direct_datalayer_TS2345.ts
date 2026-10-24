// origin: npm @lambda-team/datalayer@1.0.11 https://github.com/lambda-direct/datalayer tsconfig.bench.json seed 20261217
// flip: no
// @strict: true
// Inferring to `keyof T` from a generic key type that isn't a keyof starts over from its apparent type (tsc's
// inferFromTypes): `{ ... }[keyof TTable]`'s constraint `string | number | symbol` gives T `{ [x: string]: {} }`, so a
// generic mapped type over those keys isn't assignable to `{ [name in keyof T]: Column }` with an AbstractColumn template
declare class AbstractColumn<T> { t: T }
declare class Column<T> extends AbstractColumn<T> { autoIncrement(): void }
type Names<TTable> = { [Key in keyof TTable]: TTable[Key] extends Column<infer N> ? Key : never }[keyof TTable];
type Model<TTable> = { [Key in Names<TTable>]?: string };
declare function values<T>(columns: { [name in keyof T]: Column<number> }): T;
function insert<TTable>(mapper: { [name in keyof Model<TTable>]: AbstractColumn<number> }) {
  values(mapper);
}
declare function keyed<T>(x: keyof T): T;
function keys<TT>(n: Names<TT>) {
  const inferred = keyed(n);
}
