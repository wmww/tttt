// An untyped call checks every argument with the call (tsc's resolveUntypedCall): here the argument reads `cursor`, which
// the loop assigns from `page`, so `page` is circular, TS7022 (b4run's seed.e2e.test.ts with its store unresolved)
// @strict: true
// @target: es2022
import type { Query, Store } from "./missing";
let store: Store;
async function all(query: Query = {}): Promise<string[]> {
    const ids: string[] = [];
    let cursor: string | null = null;
    for (;;) {
        const page = await store.browse(
            cursor === null ? { ...query, limit: 500 } : { ...query, limit: 500, cursor },
        );
        ids.push(...page.records.map((record: any) => record.id));
        if (!page.continuation) return ids;
        cursor = page.continuation;
    }
}
