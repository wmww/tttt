type QueryConfigValues<T> = T extends Array<infer U> ? T : never;
interface QueryConfig<I = any[]> { name?: string; text: string; values?: QueryConfigValues<I>; }
interface QueryArrayConfig<I = any[]> extends QueryConfig<I> { rowMode: "array"; }
interface Res<R> { rows: R[] }
declare function query<R extends any[] = any[], I = any[]>(c: QueryArrayConfig<I>, values?: QueryConfigValues<I>): Promise<R>;
declare function query<R = any, I = any[]>(c: string | QueryConfig<I>, values?: QueryConfigValues<I>): Promise<R>;
declare function query<R = any, I = any[]>(c: string | QueryConfig<I>, cb: (err: Error, r: Res<R>) => void): void;
const q = { name: "n", text: "t", values: ["a", 99], rowMode: ["array"] };
query(q, (err, res) => {});
export {};
