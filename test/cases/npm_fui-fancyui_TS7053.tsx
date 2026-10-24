// origin: npm fui-fancyui@0.6.12 https://github.com/TobiTRy/fui-fancyui tsconfig.bench.json seed 20261044
// flip: no
// @allowImportingTsExtensions: true
// @isolatedModules: true
// @jsx: react-jsx
// @lib: es2020, dom, dom.iterable
// @module: esnext
// @moduleResolution: bundler
// @noFallthroughCasesInSwitch: true
// @noUnusedLocals: false
// @noUnusedParameters: false
// @resolveJsonModule: true
// @skipLibCheck: true
// @strict: true
// @target: es2020
// @types: node
// @useDefineForClassFields: true
// @preserveConstEnums: true
// @filename: node_modules/@types/node/package.json
{"name": "@types/node", "version": "26.6.5", "main": "", "types": "index.d.ts", "typesVersions": {"<=5.6": {"*": ["ts5.6/*"]}, "<=5.7": {"*": ["ts5.7/*"]}}}
// @filename: node_modules/styled-components/package.json
{"name": "styled-components", "version": "6.6.0", "types": "dist/index.d.ts", "main": "dist/styled-components.cjs.js", "module": "./dist/styled-components.esm.js"}
// @filename: package.json
{"name": "fui-fancyui", "version": "0.6.12", "type": "module", "main": "dist/lib/index.js", "types": "dist/lib/index.d.ts", "exports": {".": {"types": "./dist/lib/index.d.ts", "import": "./dist/lib/index.js", "require": "./dist/lib/index.js", "default": "./dist/lib/index.js"}, "./style.css": "./dist/fui-fancyui.css", "./package.json": "./package.json"}}
// @filename: node_modules/@types/node/index.d.ts
export {};
// @filename: node_modules/styled-components/dist/constructors/constructWithOptions.d.ts
import { Attrs, BaseObject, ExecutionProps, Interpolation, IStyledComponent, IStyledComponentFactory, KnownTarget, KnownTargetPropsWithRef, MakeAttrsOptional, MergeProps, Runtime, StyledOptions, StyledTarget, Styles, TargetProps, WidenForUntypedTarget } from '../types';
/**
 */
export interface Styled<out R extends Runtime, out Target extends StyledTarget<R>, in out OuterProps extends object, out OuterStatics extends object = BaseObject, out AttrsKeys extends keyof any = never> {
    <Props extends object = BaseObject, Statics extends object = BaseObject>(initialStyles: Styles<MergeProps<OuterProps, NoInfer<Props>>>, ...interpolations: Interpolation<MergeProps<OuterProps, NoInfer<Props>>>[]): IStyledComponent<R, WidenForUntypedTarget<TargetProps<R, Target>, MakeAttrsOptional<MergeProps<OuterProps, Props>, AttrsKeys>>> & OuterStatics & Statics & (R extends 'web' ? Target extends string ? {} : Omit<Target, keyof React.Component<any>> : {});
}
// @filename: node_modules/styled-components/dist/constructors/styled.d.ts
import { Styled as StyledInstance } from './constructWithOptions';
/**
 */
declare const baseStyled: <Target extends WebTarget, InjectedProps extends object = BaseObject>(tag: Target) => StyledInstance<"web", Target, TargetProps<"web", Target> & InjectedProps, BaseObject, never>;
declare const styled: typeof baseStyled & { [E in SupportedHTMLElements]: StyledInstance<"web", E, TargetProps<"web", E>>; };
export default styled;
// @filename: node_modules/styled-components/dist/index.d.ts
import styled, { LibraryStyled, Styled, StyledInstance } from './constructors/styled';
export { styled as default, LibraryStyled, Styled, styled, StyledInstance };
// @filename: node_modules/styled-components/dist/types.d.ts
/**
 */
export type StyledComponentBrand = {
};
export type FastOmit<T extends BaseObject, U extends string | number | symbol> = {
};
export interface ExecutionContext extends ExecutionProps {
}
export interface StyleFunction<Props extends BaseObject> {
    (executionContext: ExecutionContext & Props): Interpolation<Props>;
}
export type Interpolation<Props extends BaseObject> = StyleFunction<Props> | StyledObject<Props> | TemplateStringsArray | string | number | false | undefined | null | Keyframes | StyledComponentBrand | RuleSet<Props>;
export type RuleSet<Props extends BaseObject = BaseObject> = Interpolation<Props>[];
export interface Keyframes {
}
/**
 */
export interface StyledObject<Props extends BaseObject = BaseObject> extends CSSProperties, CSSPseudos {
}
/**
 */
export type MergeProps<A extends BaseObject, B> = keyof B extends never ? {} extends B ? A : Merged<A, B> : Merged<A, B>;
/** The taken branch of {@link MergeProps}, named so hovers print a name rather
 * than the expansion. Keep it named; see {@link Substituted}. */
export type Merged<A extends BaseObject, B> = FastOmit<A, Exclude<keyof B, 'style'>> & B;
/**
 */
// @filename: src/components/atoms/AlignedLabel/AlignedLabel.style.tsx
import { styled } from 'styled-components';
import { TAlignedLabelProps } from './TAlinedLabel.model';
import { leftRightCenterToFlexJustify } from '../../../design/designFunctions/leftRightCenterToFlexJustify';
import { TTheme } from '../../../types/TTheme';
import { TStyledPrefixAndPicker } from '../../../types/TStyledPrefixAndPicker';
type TStyledAlignedLabel = TStyledPrefixAndPicker<TAlignedLabelProps> & { theme: TTheme };
export const StyledAlignedLabel = styled.label<TStyledAlignedLabel>`
  justify-content: ${({ $align }) => leftRightCenterToFlexJustify[$align ?? 'left']};
`;
// @filename: src/components/atoms/AlignedLabel/TAlinedLabel.model.ts
import { TLayer } from '../../../types/TLayer';
import { TUiColorsSystemMessage } from '../../../types/TUiColorsSystemMessage';
import { TUiColorsNotTransparent } from '../../../types/TUiColorsNotTransparent';
import { TTextAlignLRC } from '../../../types/TTextAlignLRC';
export type TAlignedLabelProps = {
  align?: TTextAlignLRC;
  systemMessageType?: TUiColorsSystemMessage;
  themeType?: TUiColorsNotTransparent;
  layer?: TLayer;
};
// @filename: src/design/designFunctions/leftRightCenterToFlexJustify/index.ts
export { default as leftRightCenterToFlexJustify } from './leftRightCenterToFlexJustify';
// @filename: src/design/designFunctions/leftRightCenterToFlexJustify/leftRightCenterToFlexJustify.ts
import { TTextAlignLRC } from '../../../types/TTextAlignLRC';
export type TLeftRightCenterToFlexJustify = 'flex-start' | 'flex-end' | 'center';
const leftRightCenterToFlexJustify: Record<TTextAlignLRC, TLeftRightCenterToFlexJustify> = {
  left: 'flex-start',
  right: 'flex-end',
  center: 'center',
};
export default leftRightCenterToFlexJustify;
// @filename: src/design/theme/generateThemeColor/generateThemeColor.ts
import { TLayer } from '../../../types/TLayer';
const themeColors = {
  secondary: '#f0f0ef',
};
export { themeColors };
export type TthemeColorGroup = Record<TLayer, string>;
// @filename: src/types/TLayer.ts
export type TLayer = 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0;
// @filename: src/types/TStyledPrefixAndPicker.ts
type StringKeys<T> = Extract<keyof T, string>;
type RequiredKeysOfSubset<T, U extends StringKeys<T>> = {
  [K in U]: undefined extends T[K] ? never : K;
}[U];
type OptionalKeysOfSubset<T, U extends StringKeys<T>> = Exclude<U, RequiredKeysOfSubset<T, U>>;
export type TStyledPrefixAndPicker<T extends Record<string, any>, U extends StringKeys<T> = StringKeys<T>> = {
  [P in OptionalKeysOfSubset<T, U> as `$${P}`]?: T[P];
};
// @filename: src/types/TTextAlignLRC.ts
export type TTextAlignLRC = 'left' | 'right' | 'center';
// @filename: src/types/TTheme.ts
export type TTheme = {
};
// @filename: src/types/TUiColorTypes.ts
import { TUiColorsTypeObject } from './TUiColorsTypeObject';
export type TUiColorTypes = keyof TUiColorsTypeObject | 'transparent';
// @filename: src/types/TUiColorsNotTransparent.ts
import { TUiColorTypes } from './TUiColorTypes';
export type TUiColorsNotTransparent = Exclude<TUiColorTypes, 'transparent'>;
// @filename: src/types/TUiColorsSystemMessage.ts
import { TUiColorTypes } from './TUiColorTypes';
export type TUiColorsSystemMessage = Extract<TUiColorTypes, 'success' | 'error' | 'info' | 'warning'>;
// @filename: src/types/TUiColorsTypeObject.ts
import { TthemeColorGroup, themeColors } from '../design/theme/generateThemeColor/generateThemeColor';
export type TUiColorsTypeObject = { [key in keyof typeof themeColors]: TthemeColorGroup };
