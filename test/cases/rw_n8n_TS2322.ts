// origin: n8n-io/n8n at 3c7a553dc packages/cli (named projects, seed 20269001), promotion-binding-preflight.service.test.ts:83
// Known failing (text only): tsgo's head prints a spread's properties by their left declarations (`nodes: INode[]`),
// its chain line as the spread made them (`string | any` is any); tttt prints `nodes: any` in both
// @strict: true
import type { Entity, INode } from "missing-module";
type Content = Pick<Entity, "name" | "nodes" | "settings">;
interface Inv { content: Content }
function make(nodes: INode[], overrides: Partial<Inv["content"]> = {}): Inv {
    return { content: { name: "w", nodes, ...overrides } };
}
