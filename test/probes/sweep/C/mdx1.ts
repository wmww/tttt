// @noImplicitAny: true
// @strict: true
declare namespace JSX { type ElementType = any; type Element = any; type ElementClass = any; type IntrinsicElements = any; }
type ElementType = any extends JSX.ElementType ? never : JSX.ElementType;
type StringComponent = Extract<keyof JSX.IntrinsicElements, ElementType extends never ? string : ElementType>;
type FunctionComponent<Props> = ElementType extends never ? (props: Props) => JSX.Element | null : never;
type ClassComponent<Props> = ElementType extends never ? new(props: Props) => JSX.ElementClass : never;
type Component<Props> = FunctionComponent<Props> | ClassComponent<Props> | StringComponent;
interface Nested { [key: string]: Nested | Component<any>; }
type MDXComponents = Nested & { [Key in StringComponent]?: Component<JSX.IntrinsicElements[Key]> } & { wrapper?: Component<any> };
const c: MDXComponents = {
    a(props) { return null; },
    wrapper(props) { return null; },
    nested: { components: props => null },
};
