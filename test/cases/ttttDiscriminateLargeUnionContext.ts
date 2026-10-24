// A union contextual type of more than 64 members is still discriminated by an object literal's literal properties
// (tsc's discriminateTypeByDiscriminableItems has no limit): the DeepKeys-mapped union of a recursive row type,
// `validate: (name) => ...` gets its parameter from the "name" member (slotsmith's tanstack column defs)
// @strict: true
type DeepKeys<T, TDepth extends Array<any> = []> = TDepth['length'] extends 5 ? never : unknown extends T ? string : T extends Array<any> ? DeepKeys<T[number], [...TDepth, any]> : T extends object ? (keyof T & string) | DeepKeysPrefix<T, keyof T, TDepth> : never;
type DeepKeysPrefix<T, TPrefix, TDepth extends Array<any>> = TPrefix extends keyof T & (number | string) ? `${TPrefix}.${DeepKeys<T[TPrefix], [...TDepth, any]> & string}` : never;
type DeepValue<T, TProp> = T extends null | undefined ? undefined : T extends Record<string | number, any> ? TProp extends `${infer TBranch}.${infer TDeepProp}` ? DeepValue<T[TBranch], TDeepProp> : T[TProp & keyof T] : never;
interface Base<TF, TData, TValue> {}
type ColumnDefBase<TF, TData, TValue> = Base<TF, TData, TValue> & {};
interface IdIdentifier<in out TF, in out TData, TValue> {}
interface StringHeaderIdentifier {}
type ColumnIdentifiers<TF, TData, TValue> = IdIdentifier<TF, TData, TValue> | StringHeaderIdentifier;
type DisplayColumnDef<TF, TData, TValue> = ColumnDefBase<TF, TData, TValue> & ColumnIdentifiers<TF, TData, TValue>;
type GroupColumnDef<TF, TData, TValue> = ColumnDefBase<TF, TData, TValue> & {} & ColumnIdentifiers<TF, TData, TValue>;
type AccessorFnColumnDef<TF, TData, TValue> = ColumnDefBase<TF, TData, TValue> & { accessorFn: number } & ColumnIdentifiers<TF, TData, TValue>;
type AccessorKeyColumnDef<TF, TData, TValue> = ColumnDefBase<TF, TData, TValue> & {} & Partial<ColumnIdentifiers<TF, TData, TValue>>;
type ColumnDef<TF, TData, TValue = unknown> = DisplayColumnDef<TF, TData, TValue> | GroupColumnDef<TF, TData, TValue> | AccessorKeyColumnDef<TF, TData, TValue> | AccessorFnColumnDef<TF, TData, TValue>;
interface Edit<T, V> { validate?(value: V, row: T): string | null; }
type EditField<T, TValue> = true extends true
  ? { [K in DeepKeys<T>]: { accessorKey: K; edit?: boolean | Edit<T, DeepValue<T, K>> } }[DeepKeys<T>]
  : { edit?: boolean | Edit<T, TValue> };
type DataTableColumnDef<T, TValue = any> = ColumnDef<T, TValue> & EditField<T, TValue>;
type Employee = { id: string; name: string; salary: number; reports?: Employee[] };
const cols: DataTableColumnDef<Employee>[] = [
  { accessorKey: "name", edit: { validate: (name) => (name.trim() ? null : "Required") } },
];