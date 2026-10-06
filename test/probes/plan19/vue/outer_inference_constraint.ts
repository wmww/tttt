// The outer inferences a nested generic call instantiates its contextual type with are checked against their constraints
// (tsc's cloneInferenceContext + getInferredType): defineAsync's T is Comp, not the setup-shaped function (vue customElement.spec 1502)
// @target: es2015
// @lib: es2015
// @strict: true
interface Ctx { a: 1; expose: () => void }
interface FC { (props: any, ctx: Omit<Ctx, 'expose'>): any }
type Comp = FC | { setup?: (props: { x: 1 }) => void }
type Res<T> = T | { default: T }
declare function defineAsync<T extends Comp>(source: () => Promise<Res<T>>): T
declare function defineEl<P>(setup: (props: P, ctx: Ctx) => void): P
declare function defineEl(o: object): 0
export const E = defineEl(defineAsync(() => { return Promise.resolve({ setup(props) { props.x } }) }))
