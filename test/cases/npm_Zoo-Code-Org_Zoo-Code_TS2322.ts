// origin: npm Zoo-Code-Org/Zoo-Code@main https://github.com/Zoo-Code-Org/Zoo-Code webview-ui/tsconfig.bench.json seed 20261300
// flip: yes
// @strict: true
// Cut by hand from unist-util-visit 5.1.0's declarations and webview-ui's src/utils/markdown.ts: a remark plugin typed
// `(tree: Parameters<typeof visit>[0]) => void`. Parameters<> of the overloaded generic `visit` relates its erased
// overloads to `(...args: P) => any`; at a rest parameter of the generic P tsc's isInstantiatedGenericParameter holds,
// so the `visitor` parameters relate as types (BuildVisitor's alias variance), not as callbacks. tttt related them as
// callbacks, failed, and Parameters<> gave never (TS2322 in MarkdownBlock.tsx, TS2345 in a spec: the project's errors).
interface UnistNode { type: string; data?: {} | undefined; }
interface UnistParent extends UnistNode { children: UnistNode[]; }
type Test = string | null | undefined;
type VisitorResult = boolean | number | null | undefined | void;
declare function visit<Tree extends UnistNode, Check extends Test>(tree: Tree, check: Check, visitor: BuildVisitor<Tree, Check>, reverse?: boolean | null | undefined): undefined;
declare function visit<Tree extends UnistNode, Check extends Test>(tree: Tree, visitor: BuildVisitor<Tree>, reverse?: boolean | null | undefined): undefined;
type MatchesOne<Value, Check> = (Check extends null | undefined ? Value : Value extends { type: Check; } ? Value : Value extends Check ? Value : never);
type Matches<Value, Check> = (Check extends ReadonlyArray<any> ? MatchesOne<Value, Check[number]> : MatchesOne<Value, Check>);
type Uint = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10;
type Increment<I extends Uint = 0> = I extends 0 ? 1 : I extends 1 ? 2 : I extends 2 ? 3 : I extends 3 ? 4 : I extends 4 ? 5 : I extends 5 ? 6 : I extends 6 ? 7 : I extends 7 ? 8 : I extends 8 ? 9 : 10;
type InternalParent<Node extends UnistNode, Child extends UnistNode> = (Node extends UnistParent ? Node extends { children: Array<infer Children>; } ? Child extends Children ? Node : never : never : never);
type Parent<Tree extends UnistNode, Child extends UnistNode> = InternalParent<InclusiveDescendant<Tree>, Child>;
type InclusiveDescendant<Tree extends UnistNode, Max extends Uint = 10, Depth extends Uint = 0> = (Tree extends UnistParent ? Depth extends Max ? Tree : Tree | InclusiveDescendant<Tree["children"][number], Max, Increment<Depth>> : Tree);
type Visitor<Visited extends UnistNode = UnistNode, Ancestor extends UnistParent = UnistParent> = (node: Visited, index: Visited extends UnistNode ? number | undefined : never, parent: Ancestor extends UnistParent ? Ancestor | undefined : never) => VisitorResult;
type BuildVisitorFromMatch<Visited extends UnistNode, Ancestor extends UnistParent> = Visitor<Visited, Parent<Ancestor, Visited>>;
type BuildVisitorFromDescendants<Descendant extends UnistNode, Check extends Test> = (BuildVisitorFromMatch<Matches<Descendant, Check>, Extract<Descendant, UnistParent>>);
type BuildVisitor<Tree extends UnistNode = UnistNode, Check extends Test = Test> = (BuildVisitorFromDescendants<InclusiveDescendant<Tree>, Check>);
export function remarkGithubAlerts() {
	return (tree: Parameters<typeof visit>[0]) => {
		visit(tree, "blockquote", (node: any) => {})
	}
}
declare const anyTree: any;
remarkGithubAlerts()(anyTree);
type Pluggable = () => (tree: UnistNode) => void;
const plugins: Pluggable[] = [remarkGithubAlerts];
