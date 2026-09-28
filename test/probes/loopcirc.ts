// @strict: true
declare function graphql(variables: Record<string, unknown>): Promise<any>;
async function fetchAll(key: string) {
  let cursor: string | null = null;
  for (;;) {
    const data = await graphql({ cursor });
    const conn = data.repository[key];
    if (!conn.pageInfo.hasNextPage) break;
    cursor = conn.pageInfo.endCursor;
  }
}
