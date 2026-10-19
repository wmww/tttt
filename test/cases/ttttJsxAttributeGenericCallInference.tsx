// A generic call in a JSX attribute is left out of the first inference pass like one in an object literal argument
// (tsc's CheckMode.SkipGenericFunctions reaches the attributes): it sees what the other attributes inferred,
// `Table<string>` rather than `Table<unknown>`. supabase studio's UnifiedLogs.tsx `getFacetedUniqueValues={...}`
// @jsx: preserve
// @strict: true
declare namespace JSX { interface Element {} interface IntrinsicElements {} }
interface Table<TData> { rows: TData[] }
interface Ctx<TData> {
  table: Table<TData>
  f?: (table: Table<TData>) => void
}
declare function Provider<TData>(props: Ctx<TData>): JSX.Element;
declare function make<T>(): (t: Table<T>) => void;
declare const table: Table<string>;
export const a = <Provider table={table} f={make()} />;
