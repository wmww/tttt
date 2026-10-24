// origin: npm @akashnetwork/console-api@3.32.0 https://github.com/akash-network/console apps/api/tsconfig.bench.json seed 20261304
// flip: no
// @strict: true
// @target: es2022
interface TableConfig { name: string; columns: Record<string, unknown> }
declare class Table<T extends TableConfig = TableConfig> {
    readonly $inferSelect: InferSelectModel<Table<T>>;
}
type InferSelectModel<T extends Table> = { [K in keyof T["_"]["columns"]]: T["_"]["columns"][K] };
declare class PgTable<T extends TableConfig = TableConfig> extends Table<T> {}
type PgTableWithColumns<T extends TableConfig> = PgTable<T> & {
    [Key in keyof T['columns']]: T['columns'][Key];
};
export interface BaseRecordOutput<T> {}
export abstract class BaseRepository<
  T extends PgTableWithColumns<any>,
  Output extends BaseRecordOutput<string | number>
> {
  async deleteBy(query: Partial<Output>): Promise<void> {
    this.queryToWhere(query);
  }
  protected queryToWhere(query: Partial<T["$inferSelect"]> | undefined) {
  }
}
