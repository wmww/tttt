// A reused annotation whose name would be renamed through `import("...")` of a module only a nested node_modules
// reaches (TS2883) prints its type instead: tsgo's recovery boundary around the reuse drops it (graphql-inspector's
// octokit `components["schemas"]["pull-request-simple"]`, expanded by tsc)
// @declaration: true
// @module: nodenext
// @strict: true
// @filename: /node_modules/client/package.json
{ "name": "client", "types": "index.d.ts" }
// @filename: /node_modules/client/index.d.ts
import type { components } from "openapi-types";
export declare function get(): Promise<{ user: components["schemas"]["user"]; id: number }>;
// @filename: /node_modules/client/node_modules/openapi-types/package.json
{ "name": "openapi-types", "types": "index.d.ts" }
// @filename: /node_modules/client/node_modules/openapi-types/index.d.ts
export interface components {
    schemas: {
        user: { login: string; site_admin: boolean };
    };
}
// @filename: /src/index.ts
import { get } from "client";
export async function pullRequest() {
    return (await get());
}
