type RData = Record<string, number>;
type DS<D extends RData> = `DS:${keyof D & string}:G`;
interface Db<D extends RData = any> {
    update(ts: number, values: Partial<D>): void;
    update(values: D): void;
}
declare function create<D extends RData>(args: ReadonlyArray<DS<D> | "RRA">): Db<D>;
const db2 = create(["DS:test:G", "RRA"]);
db2.update(1, { test: "90" });
