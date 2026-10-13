// @strict: true
// `T[P] extends (...args: any[]) => any` over a class whose method infers its return type from a body that needs the
// mapped type being resolved: tsc's relation stops at the target's any return type and never infers it, so nothing is
// circular (dexie's NonInsertProps<T>: TS2615 depending on which file a checker saw first)
// @filename: db.ts
class Db {
  lists!: Table<InsertType<Item>>;
}
// @filename: lib.ts
type NonInsertProps<T> = { [P in keyof T]: T[P] extends (...args: any[]) => any ? P : never }[keyof T];
type InsertType<T> = Omit<T, NonInsertProps<T>>;
interface Table<I> { add(item: I): void; where(x: string): Table<I> }
declare function transaction<U>(table: Table<any>, scope: () => U): U;
// @filename: item.ts
class Item {
  db!: Db;
  name!: string;
  rename() {
    return transaction(this.db.lists, () => 1);
  }
}
const k: NonInsertProps<Item> = "rename";
