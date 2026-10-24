// origin: npm @rocket-slate/mentions@0.2.26 https://github.com/rs-pro/rocket-slate tsconfig.json seed 20261209
// flip: no
// @allowJs: true
// @importHelpers: true
// @jsx: react
// @lib: dom, es5, es6, es7, esnext
// @module: esnext
// @moduleResolution: bundler
// @noImplicitAny: false
// @noImplicitThis: true
// @noImplicitReturns: true
// @resolveJsonModule: true
// @skipLibCheck: true
// @strictNullChecks: true
// @target: es6
// @esModuleInterop: true
// @useDefineForClassFields: false
// styled(Toolbar) over @types/styled-components (ts3.7) and @types/react 16: tsgo reports TS2615 "Type of property
// 'defaultProps' circularly references itself in mapped type 'ForwardRefExoticBase<...>'"; tttt doesn't.
// Cut by reduce.py from the project and its packages.
// @filename: node_modules/@types/hoist-non-react-statics/package.json
{"name": "@types/hoist-non-react-statics", "version": "3.3.1", "main": "", "types": "index"}
// @filename: node_modules/@types/react/package.json
{"name": "@types/react", "version": "16.9.34", "main": "", "types": "index.d.ts"}
// @filename: node_modules/@types/styled-components/package.json
{"name": "@types/styled-components", "version": "4.4.3", "main": "", "types": "index.d.ts", "typesVersions": {">=3.7.0-0": {"*": ["ts3.7/*"]}}}
// @filename: node_modules/slate-plugins-next/package.json
{"name": "slate-plugins-next", "version": "0.57.15", "main": "dist/index.js", "module": "dist/index.es.js", "types": "dist/index.d.ts"}
// @filename: package.json
{"name": "rocket-slate"}
// @filename: packages/rocket-slate/package.json
{"name": "@rocket-slate/editor", "version": "0.2.17", "main": "dist/index.js", "types": "dist/index.d.ts"}
// @filename: node_modules/@types/hoist-non-react-statics/index.d.ts
declare namespace hoistNonReactStatics {
    type NonReactStatics<
        S extends React.ComponentType<any>,
    > = {
    };
}
declare function hoistNonReactStatics<
>(
): T & hoistNonReactStatics.NonReactStatics<S, C>;
export = hoistNonReactStatics;
// @filename: node_modules/@types/react/index.d.ts
export = React;
declare namespace React {
    type ComponentType<P = {}> = ComponentClass<P> | FunctionComponent<P>;
    /**
     */
    type ElementRef<
        C extends
            | keyof JSX.IntrinsicElements
    > = C extends ForwardRefExoticComponent<infer FP>
        ? FP extends RefAttributes<infer FC>
            ? FC
            : never
        : C extends { new (props: any): Component<any> }
        ? InstanceType<C>
        : C extends ((props: any, context?: any) => ReactElement | null)
        ? undefined
        : C extends keyof JSX.IntrinsicElements
        ? JSX.IntrinsicElements[C] extends DOMAttributes<infer E>
            ? E
            : never
        : never;
    interface FunctionComponent<P = {}> {
    }
    interface ComponentClass<P = {}, S = ComponentState> extends StaticLifecycle<P, S> {
    }
        /**
         */
    interface ForwardRefExoticComponent<P> extends NamedExoticComponent<P> {
        defaultProps?: Partial<P>;
    }
    type PropsWithRef<P> =
        'ref' extends keyof P
            ? P extends { ref?: infer R }
                ? string extends R
                    ? PropsWithoutRef<P> & { ref?: Exclude<R, string> }
                    : P
                : P
            : P;
    /**
     */
    type ComponentProps<T extends keyof JSX.IntrinsicElements | JSXElementConstructor<any>> =
        T extends JSXElementConstructor<infer P>
            ? P
            : T extends keyof JSX.IntrinsicElements
                ? JSX.IntrinsicElements[T]
                : {};
    type ComponentPropsWithRef<T extends ElementType> =
        T extends ComponentClass<infer P>
            ? PropsWithoutRef<P> & RefAttributes<InstanceType<T>>
            : PropsWithRef<ComponentProps<T>>;
}
type MergePropTypes<P, T> =
    P extends any
        ? MergePropTypes<P, PropTypes.InferProps<T>>
        : C extends { defaultProps: infer D; }
            ? Defaultize<P, D>
            : P;
// @filename: node_modules/@types/styled-components/ts3.7/index.d.ts
import * as React from "react";
import * as hoistNonReactStatics from 'hoist-non-react-statics';
type Defaultize<P, D> = P extends any
    ? string extends keyof P ? P :
        & Partial<Pick<P, Extract<keyof P, keyof D>>>
    : P;
export type StyledComponentProps<
    C extends keyof JSX.IntrinsicElements | React.ComponentType<any>,
    T extends object,
    O extends object,
    A extends keyof any
> =
    O extends object
    ? WithOptionalTheme<
              Partial<Pick<React.ComponentPropsWithRef<C> & O, A>>,
          T
      > &
          WithChildrenIfReactComponentClass<C>
    : never;
type WithChildrenIfReactComponentClass<
    C extends keyof JSX.IntrinsicElements | React.ComponentType<any>
> = C extends React.ComponentClass<any> ? { children?: React.ReactNode } : {};
type StyledComponentPropsWithAs<
> = StyledComponentProps<C, T, O, A> & { as?: C, forwardedAs?: C };
type ForwardRefExoticBase<P> = Pick<
    React.ForwardRefExoticComponent<P>,
    keyof React.ForwardRefExoticComponent<any>
>;
export type AnyStyledComponent =
    | StyledComponent<any, any, any>;
export type StyledComponent<
    C extends keyof JSX.IntrinsicElements | React.ComponentType<any>,
    T extends object,
    O extends object = {},
    A extends keyof any = never
> = // the "string" allows this to be used as an object key
    string & StyledComponentBase<C, T, O, A> & hoistNonReactStatics.NonReactStatics<C extends React.ComponentType<any> ? C : never>;
export interface StyledComponentBase<
    C extends keyof JSX.IntrinsicElements | React.ComponentType<any>,
    T extends object,
    O extends object = {},
    A extends keyof any = never
> extends ForwardRefExoticBase<StyledComponentProps<C, T, O, A>> {
    withComponent<WithC extends AnyStyledComponent>(
    ): StyledComponent<
        StyledComponentInnerComponent<WithC>,
        A | StyledComponentInnerAttrs<WithC>
    >;
}
export interface ThemedStyledFunction<
    A extends keyof any = never
> extends ThemedStyledFunctionBase<C, T, O, A> {
    attrs<
    >(
    ): ThemedStyledFunction<C, T, O & NewA, A | keyof NewA>;
}
export type StyledFunction<
> = ThemedStyledFunction<C, any>;
export type StyledComponentInnerComponent<
    C extends React.ComponentType<any>
> = C extends
    | StyledComponent<infer I, any, any, any>
    | StyledComponent<infer I, any, any>
    ? I
    : C;
export type StyledComponentPropsWithRef<
> = C extends AnyStyledComponent
    ? React.ComponentPropsWithRef<StyledComponentInnerComponent<C>>
    : React.ComponentPropsWithRef<C>;
export type StyledComponentInnerOtherProps<C extends AnyStyledComponent> = C extends
    | StyledComponent<any, any, infer O>
    ? O
    : never;
export type StyledComponentInnerAttrs<
    C extends AnyStyledComponent
> = C extends StyledComponent<any, any, any, infer A> ? A : never;
export interface ThemedBaseStyledInterface<T extends object>
    extends ThemedStyledComponentFactories<T> {
    <C extends AnyStyledComponent>(component: C): ThemedStyledFunction<
        StyledComponentInnerComponent<C>,
    >;
}
export type ThemedStyledInterface<T extends object> = ThemedBaseStyledInterface<
    AnyIfEmpty<T>
>;
export type StyledInterface = ThemedStyledInterface<DefaultTheme>;
export type ThemedCssFunction<T extends object> = BaseThemedCssFunction<
>;
type WithOptionalTheme<P extends { theme?: T }, T> = Omit<P, "theme"> & {
    theme?: T;
}
declare const styled: StyledInterface;
export type BaseWithThemeFnInterface<T extends object> = <
>(
) => React.ForwardRefExoticComponent<
>;
export type WithThemeFnInterface<T extends object> = BaseWithThemeFnInterface<
>;
export default styled;
// @filename: node_modules/slate-plugins-next/dist/index.d.ts
export * from './toolbar';
// @filename: node_modules/slate-plugins-next/dist/toolbar/components/Toolbar.d.ts
export declare const Toolbar: import("styled-components").StyledComponent<"div", any, ToolbarProps, never>;
// @filename: node_modules/slate-plugins-next/dist/toolbar/components/index.d.ts
export * from './Toolbar';
// @filename: node_modules/slate-plugins-next/dist/toolbar/index.d.ts
export * from './components';
// @filename: packages/rocket-slate/src/Toolbar.tsx
import { Toolbar } from 'slate-plugins-next';
import styled from 'styled-components';
const RocketToolbar = styled(Toolbar)`
`;
