// origin: n8n-io/n8n at 3c7a553dc packages/cli (named projects, seed 20269001), agent-config.service.ts:182 `const { id: agentId, projectId } = entity`
// A binding element's missing property names the apparent type: an aliased intersection with a class member is a new
// one without the alias there (tsc's getReducedApparentType in getIndexedAccessType), a property access keeps the alias
// @strict: true
class Agent { schema = ""; }
interface Thisless { schema: string }
type PA = Agent & { projectId: string };
type PT = Thisless & { projectId: string };
declare const e: PA;
declare const t: PT;
const { id } = e;
e.id;
e["id"];
const { id: id2 } = t;
function f({ nope }: PA) {}
