// origin: npm @0xsonic/sdk@0.2.6 https://github.com/sahilpabale/sonic-sdk tsconfig.bench.json seed 20261038
// flip: yes
// @forceConsistentCasingInFileNames: true
// @isolatedModules: true
// @jsx: react-jsx
// @lib: es5, es6, es7, dom, esnext
// @module: esnext
// @moduleResolution: bundler
// @noImplicitAny: true
// @noUnusedLocals: true
// @noUnusedParameters: false
// (paths is an object: not expressible as a header) {"@/*": ["./src/*"]}
// @skipLibCheck: true
// @strict: true
// @target: es6
// @types: node
// @allowSyntheticDefaultImports: true
// @esModuleInterop: true
// @preserveConstEnums: true
// @useDefineForClassFields: false
// @filename: node_modules/@chakra-ui/layout/package.json
{"name": "@chakra-ui/layout", "version": "2.1.12", "main": "dist/index.js", "module": "dist/index.mjs", "types": "dist/index.d.ts", "exports": {".": {"types": "./dist/index.d.ts", "import": "./dist/index.mjs", "require": "./dist/index.js"}, "./package.json": "./package.json"}}
// @filename: node_modules/@chakra-ui/react/package.json
{"name": "@chakra-ui/react", "version": "2.4.5", "main": "dist/index.js", "module": "dist/index.mjs", "types": "dist/index.d.ts", "exports": {".": {"types": "./dist/index.d.ts", "import": "./dist/index.mjs", "require": "./dist/index.js"}, "./package.json": "./package.json"}}
// @filename: node_modules/@chakra-ui/styled-system/package.json
{"name": "@chakra-ui/styled-system", "version": "2.5.1", "main": "dist/index.js", "module": "dist/index.mjs", "types": "dist/index.d.ts", "exports": {".": {"types": "./dist/index.d.ts", "import": "./dist/index.mjs", "require": "./dist/index.js"}, "./package.json": "./package.json"}}
// @filename: node_modules/@chakra-ui/system/package.json
{"name": "@chakra-ui/system", "version": "2.3.6", "main": "dist/index.js", "module": "dist/index.mjs", "types": "dist/index.d.ts", "exports": {".": {"types": "./dist/index.d.ts", "import": "./dist/index.mjs", "require": "./dist/index.js"}, "./package.json": "./package.json"}}
// @filename: node_modules/@types/node/package.json
{"name": "@types/node", "version": "18.11.18", "main": "", "types": "index.d.ts", "typesVersions": {"<=4.8": {"*": ["ts4.8/*"]}}}
// @filename: node_modules/@types/react/package.json
{"name": "@types/react", "version": "18.0.26", "main": "", "types": "index.d.ts", "exports": {".": {"types": {"default": "./index.d.ts"}}, "./next": {"types": {"default": "./next.d.ts"}}, "./experimental": {"types": {"default": "./experimental.d.ts"}}, "./jsx-runtime": {"types": {"default": "./jsx-runtime.d.ts"}}, "./jsx-dev-runtime": {"types": {"default": "./jsx-dev-runtime.d.ts"}}, "./package.json": "./package.json"}}
// @filename: package.json
{"name": "@0xsonic/sdk", "version": "0.2.6", "main": "dist/index.js", "types": "dist/index.d.ts", "exports": {".": {"import": "./dist/index.js", "require": "./dist/index.mjs"}}}
// @filename: node_modules/@chakra-ui/layout/dist/index.d.ts
export { Text, TextProps } from './text.js';
// @filename: node_modules/@chakra-ui/layout/dist/text.d.ts
import * as _chakra_ui_system from '@chakra-ui/system';
import { HTMLChakraProps, ThemingProps, SystemProps } from '@chakra-ui/system';
interface TextProps extends HTMLChakraProps<"p">, ThemingProps<"Text"> {
    /**
     */
}
declare const Text: _chakra_ui_system.ComponentWithAs<"p", TextProps>;
// @filename: node_modules/@chakra-ui/react/dist/index.d.ts
export * from '@chakra-ui/layout';
// @filename: node_modules/@chakra-ui/styled-system/dist/index.d.ts
interface StyleProps extends SpaceProps, ColorProps, TransitionProps, TypographyProps, FlexboxProps, TransformProps, GridProps, FilterProps, LayoutProps, BorderProps, EffectProps, BackgroundProps, ListProps, PositionProps, RingProps, ScrollProps, InteractivityProps, TextDecorationProps, OtherProps {
}
export { BackgroundProps, BorderProps, CSSObject, CSSWithMultiValues, ColorProps, CustomThemeTypings, EffectProps, FilterProps, FlatToken, FlatTokens, FlattenTokensParam, FlexboxProps, FunctionCSSInterpolation, GridProps, InteractivityProps, LayoutProps, ListProps, MultiStyleConfig, OmitSpaceXY, Operand, OtherProps, PartsStyleFunction, PartsStyleInterpolation, PartsStyleObject, PlainToken, PositionProps, Pseudos, RecursiveCSSObject, RecursiveCSSSelector, RecursivePseudo, ResponsiveArray, ResponsiveObject, ResponsiveValue, RingProps, ScrollProps, SemanticToken, SemanticValue, SpaceProps, StyleConfig, StyleFunctionProps, StyleObjectOrFn, StyleProps, SystemCSSProperties, SystemProps, SystemStyleFunction, SystemStyleInterpolation, SystemStyleObject, TextDecorationProps, ThemeScale, ThemeThunk, ThemeTypings, ThemingProps, TransformProps, TransitionProps, TypographyProps, WithCSSVar, addPrefix, background, border, calc, color, createMultiStyleConfigHelpers, css, cssVar, defineStyle, defineStyleConfig, effect, filter, flatten, flattenTokens, flexbox, getCSSVar, getCss, grid, interactivity, isStyleProp, layout, layoutPropNames, list, omitThemingProps, others, position, propNames, pseudoPropNames, pseudoSelectors, resolveStyleConfig, ring, scroll, space, systemProps, textDecoration, toCSSVar, toVarDefinition, toVarReference, tokenToCSSVar, transform, transition, typography };
// @filename: node_modules/@chakra-ui/system/dist/index.d.ts
export { As, ChakraComponent, ChakraProps, ComponentWithAs, MergeWithAs, OmitCommonProps, PropsOf, RightJoinProps } from './system.types.js';
export { ChakraStyledOptions, HTMLChakraComponents, HTMLChakraProps, styled, toCSSObject } from './system.js';
import 'react';
// @filename: node_modules/@chakra-ui/system/dist/system.d.ts
import { SystemStyleObject, StyleProps } from '@chakra-ui/styled-system';
import { As, ChakraComponent, PropsOf, ChakraProps } from './system.types.js';
/**
 */
declare type HTMLChakraProps<T extends As> = Omit<PropsOf<T>, "ref" | keyof StyleProps> & ChakraProps & {
    as?: As;
};
// @filename: node_modules/@chakra-ui/system/dist/system.types.d.ts
import { Interpolation } from '@emotion/react';
interface ChakraProps extends SystemProps {
}
declare type As<Props = any> = React.ElementType<Props>;
declare type PropsOf<T extends As> = React.ComponentPropsWithoutRef<T> & {
    as?: As;
};
declare type OmitCommonProps<Target, OmitAdditionalProps extends keyof any = never> = Omit<Target, "transition" | "as" | "color" | OmitAdditionalProps>;
declare type RightJoinProps<SourceProps extends object = {}, OverrideProps extends object = {}> = OmitCommonProps<SourceProps, keyof OverrideProps> & OverrideProps;
declare type MergeWithAs<ComponentProps extends object, AsProps extends object, AdditionalProps extends object = {}, AsComponent extends As = As> = RightJoinProps<ComponentProps, AdditionalProps> & RightJoinProps<AsProps, AdditionalProps> & {
    as?: AsComponent;
};
declare type ComponentWithAs<Component extends As, Props extends object = {}> = {
    <AsComponent extends As = Component>(props: MergeWithAs<React.ComponentProps<Component>, React.ComponentProps<AsComponent>, Props, AsComponent>): JSX.Element;
};
// @filename: node_modules/@types/node/index.d.ts
export {};
// @filename: node_modules/@types/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    type ElementType<P = any> =
        {
            [K in keyof JSX.IntrinsicElements]: P extends JSX.IntrinsicElements[K] ? K : never
        }[keyof JSX.IntrinsicElements] |
        ComponentType<P>;
    type ComponentType<P = {}> = ComponentClass<P> | FunctionComponent<P>;
    type JSXElementConstructor<P> =
        | (new (props: P) => Component<any, any>);
    /**
     */
    interface ExoticComponent<P = {}> {
        (props: P): (ReactElement|null);
    }
    interface NamedExoticComponent<P = {}> extends ExoticComponent<P> {
    }
    interface FunctionComponent<P = {}> {
    }
    interface ComponentClass<P = {}, S = ComponentState> extends StaticLifecycle<P, S> {
    }
    type PropsWithoutRef<P> =
        P extends any ? ('ref' extends keyof P ? Pick<P, Exclude<keyof P, 'ref'>> : P) : P;
    type PropsWithRef<P> =
        'ref' extends keyof P
            ? P extends { ref?: infer R | undefined }
                ? string extends R
                    ? PropsWithoutRef<P> & { ref?: Exclude<R, string> | undefined }
                    : P
                : P
            : P;
    type ComponentProps<T extends keyof JSX.IntrinsicElements | JSXElementConstructor<any>> =
        T extends JSXElementConstructor<infer P>
                ? JSX.IntrinsicElements[T]
                : {};
    type ComponentPropsWithRef<T extends ElementType> =
        T extends (new (props: infer P) => Component<any, any>)
            ? PropsWithoutRef<P> & RefAttributes<InstanceType<T>>
            : PropsWithRef<ComponentProps<T>>;
    type ComponentPropsWithoutRef<T extends ElementType> =
        PropsWithoutRef<ComponentProps<T>>;
    type ComponentRef<T extends ElementType> = T extends NamedExoticComponent<
    >
            ? Method
            : never;
    type MemoExoticComponent<T extends ComponentType<any>> = NamedExoticComponent<ComponentPropsWithRef<T>> & {
    };
    type LazyExoticComponent<T extends ComponentType<any>> = ExoticComponent<ComponentPropsWithRef<T>> & {
    };
    type AriaRole =
        | 'alert'
}
type MergePropTypes<P, T> =
    P extends any
            ? Defaultize<P, D>
            : P;
declare global {
    namespace JSX {
        type LibraryManagedAttributes<C, P> = C extends React.MemoExoticComponent<infer T> | React.LazyExoticComponent<infer T>
            ? T extends React.MemoExoticComponent<infer U> | React.LazyExoticComponent<infer U>
                ? ReactManagedAttributes<U, P>
                : ReactManagedAttributes<T, P>
            : ReactManagedAttributes<C, P>;
        interface IntrinsicElements {
            abbr: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            address: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            area: React.DetailedHTMLProps<React.AreaHTMLAttributes<HTMLAreaElement>, HTMLAreaElement>;
            bdo: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            big: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            blockquote: React.DetailedHTMLProps<React.BlockquoteHTMLAttributes<HTMLQuoteElement>, HTMLQuoteElement>;
            body: React.DetailedHTMLProps<React.HTMLAttributes<HTMLBodyElement>, HTMLBodyElement>;
            br: React.DetailedHTMLProps<React.HTMLAttributes<HTMLBRElement>, HTMLBRElement>;
            button: React.DetailedHTMLProps<React.ButtonHTMLAttributes<HTMLButtonElement>, HTMLButtonElement>;
            canvas: React.DetailedHTMLProps<React.CanvasHTMLAttributes<HTMLCanvasElement>, HTMLCanvasElement>;
            caption: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            center: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            cite: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            code: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            hgroup: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            hr: React.DetailedHTMLProps<React.HTMLAttributes<HTMLHRElement>, HTMLHRElement>;
            html: React.DetailedHTMLProps<React.HtmlHTMLAttributes<HTMLHtmlElement>, HTMLHtmlElement>;
            i: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            iframe: React.DetailedHTMLProps<React.IframeHTMLAttributes<HTMLIFrameElement>, HTMLIFrameElement>;
            img: React.DetailedHTMLProps<React.ImgHTMLAttributes<HTMLImageElement>, HTMLImageElement>;
            input: React.DetailedHTMLProps<React.InputHTMLAttributes<HTMLInputElement>, HTMLInputElement>;
            ins: React.DetailedHTMLProps<React.InsHTMLAttributes<HTMLModElement>, HTMLModElement>;
            kbd: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            keygen: React.DetailedHTMLProps<React.KeygenHTMLAttributes<HTMLElement>, HTMLElement>;
            label: React.DetailedHTMLProps<React.LabelHTMLAttributes<HTMLLabelElement>, HTMLLabelElement>;
            legend: React.DetailedHTMLProps<React.HTMLAttributes<HTMLLegendElement>, HTMLLegendElement>;
            li: React.DetailedHTMLProps<React.LiHTMLAttributes<HTMLLIElement>, HTMLLIElement>;
            link: React.DetailedHTMLProps<React.LinkHTMLAttributes<HTMLLinkElement>, HTMLLinkElement>;
            main: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            map: React.DetailedHTMLProps<React.MapHTMLAttributes<HTMLMapElement>, HTMLMapElement>;
            mark: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            menu: React.DetailedHTMLProps<React.MenuHTMLAttributes<HTMLElement>, HTMLElement>;
            menuitem: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            meta: React.DetailedHTMLProps<React.MetaHTMLAttributes<HTMLMetaElement>, HTMLMetaElement>;
            meter: React.DetailedHTMLProps<React.MeterHTMLAttributes<HTMLMeterElement>, HTMLMeterElement>;
            nav: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            noindex: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            noscript: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            object: React.DetailedHTMLProps<React.ObjectHTMLAttributes<HTMLObjectElement>, HTMLObjectElement>;
            ol: React.DetailedHTMLProps<React.OlHTMLAttributes<HTMLOListElement>, HTMLOListElement>;
            optgroup: React.DetailedHTMLProps<React.OptgroupHTMLAttributes<HTMLOptGroupElement>, HTMLOptGroupElement>;
            option: React.DetailedHTMLProps<React.OptionHTMLAttributes<HTMLOptionElement>, HTMLOptionElement>;
            output: React.DetailedHTMLProps<React.OutputHTMLAttributes<HTMLOutputElement>, HTMLOutputElement>;
            p: React.DetailedHTMLProps<React.HTMLAttributes<HTMLParagraphElement>, HTMLParagraphElement>;
            param: React.DetailedHTMLProps<React.ParamHTMLAttributes<HTMLParamElement>, HTMLParamElement>;
            picture: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            pre: React.DetailedHTMLProps<React.HTMLAttributes<HTMLPreElement>, HTMLPreElement>;
            progress: React.DetailedHTMLProps<React.ProgressHTMLAttributes<HTMLProgressElement>, HTMLProgressElement>;
            q: React.DetailedHTMLProps<React.QuoteHTMLAttributes<HTMLQuoteElement>, HTMLQuoteElement>;
            rp: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            rt: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            ruby: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            s: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            samp: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            slot: React.DetailedHTMLProps<React.SlotHTMLAttributes<HTMLSlotElement>, HTMLSlotElement>;
            script: React.DetailedHTMLProps<React.ScriptHTMLAttributes<HTMLScriptElement>, HTMLScriptElement>;
            section: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            select: React.DetailedHTMLProps<React.SelectHTMLAttributes<HTMLSelectElement>, HTMLSelectElement>;
            small: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            source: React.DetailedHTMLProps<React.SourceHTMLAttributes<HTMLSourceElement>, HTMLSourceElement>;
            span: React.DetailedHTMLProps<React.HTMLAttributes<HTMLSpanElement>, HTMLSpanElement>;
            strong: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            style: React.DetailedHTMLProps<React.StyleHTMLAttributes<HTMLStyleElement>, HTMLStyleElement>;
            sub: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            summary: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            sup: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            table: React.DetailedHTMLProps<React.TableHTMLAttributes<HTMLTableElement>, HTMLTableElement>;
            template: React.DetailedHTMLProps<React.HTMLAttributes<HTMLTemplateElement>, HTMLTemplateElement>;
            tbody: React.DetailedHTMLProps<React.HTMLAttributes<HTMLTableSectionElement>, HTMLTableSectionElement>;
            td: React.DetailedHTMLProps<React.TdHTMLAttributes<HTMLTableDataCellElement>, HTMLTableDataCellElement>;
            textarea: React.DetailedHTMLProps<React.TextareaHTMLAttributes<HTMLTextAreaElement>, HTMLTextAreaElement>;
            tfoot: React.DetailedHTMLProps<React.HTMLAttributes<HTMLTableSectionElement>, HTMLTableSectionElement>;
            th: React.DetailedHTMLProps<React.ThHTMLAttributes<HTMLTableHeaderCellElement>, HTMLTableHeaderCellElement>;
            thead: React.DetailedHTMLProps<React.HTMLAttributes<HTMLTableSectionElement>, HTMLTableSectionElement>;
            time: React.DetailedHTMLProps<React.TimeHTMLAttributes<HTMLTimeElement>, HTMLTimeElement>;
            title: React.DetailedHTMLProps<React.HTMLAttributes<HTMLTitleElement>, HTMLTitleElement>;
            tr: React.DetailedHTMLProps<React.HTMLAttributes<HTMLTableRowElement>, HTMLTableRowElement>;
            track: React.DetailedHTMLProps<React.TrackHTMLAttributes<HTMLTrackElement>, HTMLTrackElement>;
            u: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            ul: React.DetailedHTMLProps<React.HTMLAttributes<HTMLUListElement>, HTMLUListElement>;
            "var": React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            video: React.DetailedHTMLProps<React.VideoHTMLAttributes<HTMLVideoElement>, HTMLVideoElement>;
            wbr: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            webview: React.DetailedHTMLProps<React.WebViewHTMLAttributes<HTMLWebViewElement>, HTMLWebViewElement>;
            animate: React.SVGProps<SVGElement>; // TODO: It is SVGAnimateElement but is not in TypeScript's lib.dom.d.ts for now.
            animateMotion: React.SVGProps<SVGElement>;
            animateTransform: React.SVGProps<SVGElement>; // TODO: It is SVGAnimateTransformElement but is not in TypeScript's lib.dom.d.ts for now.
            circle: React.SVGProps<SVGCircleElement>;
            clipPath: React.SVGProps<SVGClipPathElement>;
            defs: React.SVGProps<SVGDefsElement>;
            desc: React.SVGProps<SVGDescElement>;
            ellipse: React.SVGProps<SVGEllipseElement>;
            feBlend: React.SVGProps<SVGFEBlendElement>;
            feColorMatrix: React.SVGProps<SVGFEColorMatrixElement>;
            feComponentTransfer: React.SVGProps<SVGFEComponentTransferElement>;
            feComposite: React.SVGProps<SVGFECompositeElement>;
            feConvolveMatrix: React.SVGProps<SVGFEConvolveMatrixElement>;
            feDiffuseLighting: React.SVGProps<SVGFEDiffuseLightingElement>;
            feDisplacementMap: React.SVGProps<SVGFEDisplacementMapElement>;
            feDistantLight: React.SVGProps<SVGFEDistantLightElement>;
            feDropShadow: React.SVGProps<SVGFEDropShadowElement>;
            feFlood: React.SVGProps<SVGFEFloodElement>;
            feFuncA: React.SVGProps<SVGFEFuncAElement>;
            feFuncB: React.SVGProps<SVGFEFuncBElement>;
            feFuncG: React.SVGProps<SVGFEFuncGElement>;
            feFuncR: React.SVGProps<SVGFEFuncRElement>;
            feGaussianBlur: React.SVGProps<SVGFEGaussianBlurElement>;
            feImage: React.SVGProps<SVGFEImageElement>;
            feMerge: React.SVGProps<SVGFEMergeElement>;
            feMergeNode: React.SVGProps<SVGFEMergeNodeElement>;
            feMorphology: React.SVGProps<SVGFEMorphologyElement>;
            feOffset: React.SVGProps<SVGFEOffsetElement>;
            fePointLight: React.SVGProps<SVGFEPointLightElement>;
            feSpecularLighting: React.SVGProps<SVGFESpecularLightingElement>;
            feSpotLight: React.SVGProps<SVGFESpotLightElement>;
            feTile: React.SVGProps<SVGFETileElement>;
            feTurbulence: React.SVGProps<SVGFETurbulenceElement>;
            filter: React.SVGProps<SVGFilterElement>;
            foreignObject: React.SVGProps<SVGForeignObjectElement>;
            g: React.SVGProps<SVGGElement>;
            image: React.SVGProps<SVGImageElement>;
            line: React.SVGProps<SVGLineElement>;
            linearGradient: React.SVGProps<SVGLinearGradientElement>;
            marker: React.SVGProps<SVGMarkerElement>;
            mask: React.SVGProps<SVGMaskElement>;
            metadata: React.SVGProps<SVGMetadataElement>;
            mpath: React.SVGProps<SVGElement>;
            path: React.SVGProps<SVGPathElement>;
            pattern: React.SVGProps<SVGPatternElement>;
            polygon: React.SVGProps<SVGPolygonElement>;
            polyline: React.SVGProps<SVGPolylineElement>;
            radialGradient: React.SVGProps<SVGRadialGradientElement>;
            rect: React.SVGProps<SVGRectElement>;
            stop: React.SVGProps<SVGStopElement>;
            switch: React.SVGProps<SVGSwitchElement>;
            symbol: React.SVGProps<SVGSymbolElement>;
            text: React.SVGProps<SVGTextElement>;
            textPath: React.SVGProps<SVGTextPathElement>;
            tspan: React.SVGProps<SVGTSpanElement>;
            use: React.SVGProps<SVGUseElement>;
            view: React.SVGProps<SVGViewElement>;
        }
    }
}
// @filename: src/components/User.tsx
import { Text } from '@chakra-ui/react';
  return <Text>Connected to {user.did}</Text>;
