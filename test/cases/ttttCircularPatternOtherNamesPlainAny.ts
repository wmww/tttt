// @strict: true
// @target: es2022
// A destructuring whose parent is circular through one of its names: only the names on the cycle are TS7022 (tsc's
// reportCircularityError gives the rest plain any), not `nodes` (crowd-dev's paginating loops)
declare function gql<T>(vars: { cursor: string | null }): Promise<T>;
interface Page { repository: { issues: { pageInfo: { hasNextPage: boolean; endCursor: string | null }; nodes: (string | null)[] } } }
declare function budget(): boolean;
async function run() {
  let cursor: string | null = null;
  while (budget()) {
    const data = await gql<Page>({ cursor });
    const { pageInfo, nodes } = data.repository.issues;
    const items = nodes.filter((n): n is string => n !== null);
    cursor = pageInfo.hasNextPage ? pageInfo.endCursor : null;
    if (!pageInfo.hasNextPage) return items;
  }
}
