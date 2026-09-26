// @strict: true
// Storybook: ComponentAnnotations.component is `(TRenderer & { T: TArgs })['component']` with
// `interface ReactRenderer { component: ComponentType<this['T']> }`: the property read off the intersection has
// `this` = the whole intersection, so this['T'] = TArgs
type Renderer = { component: unknown; T?: unknown };
interface ReactRenderer extends Renderer { component: (props: this['T']) => void; }
type Comp<R extends Renderer, TArgs> = (R & { T: TArgs })['component'];
declare const c: Comp<ReactRenderer, { size: string }>;
const ok: (props: { size: string }) => void = c;
c({ size: "md" });

declare const i: ReactRenderer & { T: { size: string } };
const viaValue = i.component;
type ViaType = (ReactRenderer & { T: { size: string } })['component'];

// The corpus error: `const meta: Meta<typeof Spinner> = { component: Spinner }`
declare function Spinner(props: { size: string }): void;
const meta: { component?: Comp<ReactRenderer, { size: string }> } = { component: Spinner };
