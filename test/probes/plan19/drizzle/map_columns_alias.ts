// @strict: true
declare class Column<T = any> { _: T }
interface TableConfig { name: string; columns: Record<string, Column<any>> }
declare class Table<T extends TableConfig = TableConfig> { _: { columns: T["columns"]; config: T } }
type ColumnsSelection = Record<string, unknown>;
type Map<TColumns extends ColumnsSelection, TAlias extends string> = {
    [Key in keyof TColumns]: TColumns[Key] extends Column ? Column<TAlias> : TColumns[Key];
} & {};
type Upd<T extends Partial<TableConfig>> = T;
type Build<TTable extends Table, TAlias extends string> = Upd<{ name: TAlias; columns: Map<TTable["_"]["columns"], TAlias> }>;
type Build2<TC extends Record<string, Column<any>>, TAlias extends string> = Upd<{ name: TAlias; columns: Map<TC, TAlias> }>;
