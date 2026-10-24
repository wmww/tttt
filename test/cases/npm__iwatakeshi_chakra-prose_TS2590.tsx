// origin: npm @iwatakeshi/chakra-prose@0.0.2 https://github.com/iwatakeshi/chakra-prose tsconfig.bench.json seed 20261037
// flip: no
// @forceConsistentCasingInFileNames: true
// @jsx: preserve
// @module: commonjs
// @skipLibCheck: true
// @strict: true
// @target: es2016
// @esModuleInterop: true
// @useDefineForClassFields: false
// @filename: node_modules/@chakra-ui/layout/package.json
{"name": "@chakra-ui/layout", "version": "2.0.0", "main": "dist/chakra-ui-layout.cjs.js", "module": "dist/chakra-ui-layout.esm.js", "types": "dist/chakra-ui-layout.cjs.d.ts"}
// @filename: node_modules/@chakra-ui/react/package.json
{"name": "@chakra-ui/react", "version": "2.0.2", "main": "dist/chakra-ui-react.cjs.js", "module": "dist/chakra-ui-react.esm.js", "types": "dist/chakra-ui-react.cjs.d.ts"}
// @filename: node_modules/@chakra-ui/styled-system/package.json
{"name": "@chakra-ui/styled-system", "version": "2.0.0", "main": "dist/chakra-ui-styled-system.cjs.js", "module": "dist/chakra-ui-styled-system.esm.js", "types": "dist/chakra-ui-styled-system.cjs.d.ts"}
// @filename: node_modules/@chakra-ui/system/package.json
{"name": "@chakra-ui/system", "version": "2.0.2", "main": "dist/chakra-ui-system.cjs.js", "module": "dist/chakra-ui-system.esm.js", "types": "dist/chakra-ui-system.cjs.d.ts"}
// @filename: node_modules/@types/react/package.json
{"name": "@types/react", "version": "18.0.9", "main": "", "types": "index.d.ts", "exports": {".": {"types": {"default": "./index.d.ts"}}, "./next": {"types": {"default": "./next.d.ts"}}, "./experimental": {"types": {"default": "./experimental.d.ts"}}, "./jsx-runtime": {"types": {"default": "./jsx-runtime.d.ts"}}, "./jsx-dev-runtime": {"types": {"default": "./jsx-dev-runtime.d.ts"}}, "./package.json": "./package.json"}}
// @filename: package.json
{"name": "@iwatakeshi/chakra-prose", "version": "0.0.1", "main": "dist/index.js", "module": "dist/module.js", "types": "dist/types.d.ts"}
// @filename: node_modules/@chakra-ui/layout/dist/chakra-ui-layout.cjs.d.ts
export * from "./declarations/src/index";
// @filename: node_modules/@chakra-ui/layout/dist/declarations/src/index.d.ts
export * from "./list";
// @filename: node_modules/@chakra-ui/layout/dist/declarations/src/list.d.ts
import { HTMLChakraProps, SystemProps, ThemingProps } from "@chakra-ui/system";
export interface ListProps extends HTMLChakraProps<"ul">, ThemingProps<"List">, ListOptions {
}
/**
 */
export declare const UnorderedList: import("@chakra-ui/system").ComponentWithAs<"ul", ListProps>;
// @filename: node_modules/@chakra-ui/react/dist/chakra-ui-react.cjs.d.ts
export * from "./declarations/src/index";
// @filename: node_modules/@chakra-ui/react/dist/declarations/src/index.d.ts
export * from "@chakra-ui/layout";
// @filename: node_modules/@chakra-ui/styled-system/dist/chakra-ui-styled-system.cjs.d.ts
export * from "./declarations/src/index";
// @filename: node_modules/@chakra-ui/styled-system/dist/declarations/src/index.d.ts
export * from "./system.types";
// @filename: node_modules/@chakra-ui/styled-system/dist/declarations/src/system.types.d.ts
export interface StyleProps extends SpaceProps, ColorProps, TransitionProps, TypographyProps, FlexboxProps, TransformProps, GridProps, FilterProps, LayoutProps, BorderProps, EffectProps, BackgroundProps, ListProps, PositionProps, RingProps, ScrollProps, InteractivityProps, TextDecorationProps, OtherProps {
}
// @filename: node_modules/@chakra-ui/system/dist/chakra-ui-system.cjs.d.ts
export * from "./declarations/src/index";
// @filename: node_modules/@chakra-ui/system/dist/declarations/src/index.d.ts
export * from "./system.types";
export * from "./system";
// @filename: node_modules/@chakra-ui/system/dist/declarations/src/system.d.ts
import { StyleProps, SystemStyleObject } from "@chakra-ui/styled-system";
import { As, ChakraComponent, ChakraProps, PropsOf } from "./system.types";
/**
 */
export declare type HTMLChakraProps<T extends As> = Omit<PropsOf<T>, "ref" | keyof StyleProps> & ChakraProps & {
    as?: As;
};
// @filename: node_modules/@chakra-ui/system/dist/declarations/src/system.types.d.ts
import * as React from "react";
export interface ChakraProps extends SystemProps {
}
export declare type As<Props = any> = React.ElementType<Props>;
export declare type PropsOf<T extends As> = React.ComponentPropsWithoutRef<T> & {
    as?: As;
};
export declare type OmitCommonProps<Target, OmitAdditionalProps extends keyof any = never> = Omit<Target, "transition" | "as" | "color" | OmitAdditionalProps>;
export declare type RightJoinProps<SourceProps extends object = {}, OverrideProps extends object = {}> = OmitCommonProps<SourceProps, keyof OverrideProps> & OverrideProps;
export declare type MergeWithAs<ComponentProps extends object, AsProps extends object, AdditionalProps extends object = {}, AsComponent extends As = As> = RightJoinProps<ComponentProps, AdditionalProps> & RightJoinProps<AsProps, AdditionalProps> & {
    as?: AsComponent;
};
export declare type ComponentWithAs<Component extends As, Props extends object = {}> = {
    <AsComponent extends As = Component>(props: MergeWithAs<React.ComponentProps<Component>, React.ComponentProps<AsComponent>, Props, AsComponent>): JSX.Element;
};
// @filename: node_modules/@types/react/index.d.ts
export = React;
declare namespace React {
    type ElementType<P = any> =
        {
            [K in keyof JSX.IntrinsicElements]: P extends JSX.IntrinsicElements[K] ? K : never
        }[keyof JSX.IntrinsicElements] |
        ComponentType<P>;
    type ComponentType<P = {}> = ComponentClass<P> | FunctionComponent<P>;
    /**
     */
    interface ReactComponentElement<
    > extends ReactElement<P, Exclude<T, number>> { }
    interface ExoticComponent<P = {}> {
        (props: P): (ReactElement|null);
    }
    interface NamedExoticComponent<P = {}> extends ExoticComponent<P> {
    }
    interface FunctionComponent<P = {}> {
    }
    /**
     */
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
    /**
     */
    type ComponentProps<T extends keyof JSX.IntrinsicElements | JSXElementConstructor<any>> =
        T extends JSXElementConstructor<infer P>
            ? P
            : T extends keyof JSX.IntrinsicElements
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
        : ComponentPropsWithRef<T> extends RefAttributes<infer Method>
            ? Method
            : never;
    type MemoExoticComponent<T extends ComponentType<any>> = NamedExoticComponent<ComponentPropsWithRef<T>> & {
    };
    type LazyExoticComponent<T extends ComponentType<any>> = ExoticComponent<ComponentPropsWithRef<T>> & {
    };
    /**
     */
    type AriaRole =
        | 'alert'
}
type MergePropTypes<P, T> =
    P extends any
        ? IsExactlyAny<P> extends true ? T :
                & Pick<P, Exclude<keyof P, keyof T>>
    : C extends { propTypes: infer T; }
        ? MergePropTypes<P, PropTypes.InferProps<T>>
        : C extends { defaultProps: infer D; }
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
            dfn: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            dialog: React.DetailedHTMLProps<React.DialogHTMLAttributes<HTMLDialogElement>, HTMLDialogElement>;
            div: React.DetailedHTMLProps<React.HTMLAttributes<HTMLDivElement>, HTMLDivElement>;
            figure: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            footer: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
            form: React.DetailedHTMLProps<React.FormHTMLAttributes<HTMLFormElement>, HTMLFormElement>;
            h1: React.DetailedHTMLProps<React.HTMLAttributes<HTMLHeadingElement>, HTMLHeadingElement>;
            h2: React.DetailedHTMLProps<React.HTMLAttributes<HTMLHeadingElement>, HTMLHeadingElement>;
            h3: React.DetailedHTMLProps<React.HTMLAttributes<HTMLHeadingElement>, HTMLHeadingElement>;
            h4: React.DetailedHTMLProps<React.HTMLAttributes<HTMLHeadingElement>, HTMLHeadingElement>;
            h5: React.DetailedHTMLProps<React.HTMLAttributes<HTMLHeadingElement>, HTMLHeadingElement>;
            h6: React.DetailedHTMLProps<React.HTMLAttributes<HTMLHeadingElement>, HTMLHeadingElement>;
            head: React.DetailedHTMLProps<React.HTMLAttributes<HTMLHeadElement>, HTMLHeadElement>;
            header: React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement>;
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
// @filename: src/stories/Page.tsx
import { Box, Button, ListItem, Text, UnorderedList } from '@chakra-ui/react';
  return <Box p={10}>
    <Prose>
      <p>
      </p>
      <UnorderedList>
      </UnorderedList>
    </Prose>
  </Box >
