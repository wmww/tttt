// origin: n8n-io/n8n at 3c7a553dc packages/cli (named projects, seed 20269001), e.g. src/modules/agents/agent-config.service.ts:68
// @strict: true
// An aliased intersection with a class or interface member is named without its alias inside the relation (tsc's
// getApparentTypeOfIntersectionType gives the members `this`): the missing property's line doesn't repeat the head's
// types, so an argument keeps its TS2345 head. With anonymous members only, the alias stays and TS2741 is the head.
declare const WithBase: any;
class Agent extends WithBase { schema: string = ""; projectId: string | null = null; }
class Plain { schema: string = ""; projectId: string | null = null; }
interface Iface { schema: string }
interface T1 { id: string; schema: string }
declare function f(a: T1): void;
declare function g(a: Pick<Agent, "id" | "schema">): void;
type ProjectScoped<T extends Pick<Agent, "projectId">> = T & { projectId: string };
type ProjectAgent = ProjectScoped<Agent>;
declare const pa: ProjectAgent;
f(pa);
g(pa);
type PB = Plain & { projectId: string };
declare const pb: PB;
f(pb);
type PI = Iface & { projectId: string };
declare const pi: PI;
f(pi);
type PD = { schema: string } & { projectId: string };
declare const pd: PD;
f(pd);
declare const unaliased: Agent & { projectId: string };
f(unaliased);
const x: T1 = pb;
