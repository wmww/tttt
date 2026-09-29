// @strict: true
declare class Column<T = any> { _: T }
interface TableConfig<TColumn extends Column = Column<any>> { name: string; columns: Record<string, TColumn> }
declare class Table<T extends TableConfig = TableConfig> { declare readonly _: { readonly columns: T["columns"]; readonly config: T } }
declare class PgTable<T extends TableConfig = TableConfig> extends Table<T> { p: 1 }
type Map<TColumns extends Record<string, unknown>> = {
    [Key in keyof TColumns]: TColumns[Key] extends Column ? Column : TColumns[Key];
} & {};
type Chk<T extends Partial<TableConfig>> = T;
type B4<TTable extends PgTable> = TTable extends Table ? Chk<{ columns: Map<TTable["_"]["columns"]> }> : never;
