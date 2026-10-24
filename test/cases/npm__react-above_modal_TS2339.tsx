// origin: npm @react-above/modal@0.3.9 https://github.com/react-above/modal tsconfig.bench.json seed 20261109
// flip: no
// @allowJs: false
// @forceConsistentCasingInFileNames: true
// @isolatedModules: true
// @jsx: react
// @lib: dom, dom.iterable, esnext
// @module: esnext
// @moduleResolution: bundler
// (paths is an object: not expressible as a header) {"*": ["./*"]}
// @resolveJsonModule: true
// @skipLibCheck: true
// @strict: true
// @target: es6
// @typeRoots: ./node_modules/@types
// @allowSyntheticDefaultImports: true
// @esModuleInterop: true
// @preserveConstEnums: true
// @useDefineForClassFields: false
// @filename: node_modules/@types/react/package.json
{"name": "@types/react", "version": "16.14.25", "main": "", "types": "index.d.ts"}
// @filename: package.json
{"name": "@react-above/modal", "version": "0.0.0-set-by-ci", "main": "./dist/lib/index.js", "exports": "./dist/lib/index.mjs", "module": "./dist/lib/index.js", "types": "./dist/lib/index.d.ts"}
// @filename: demo/index.tsx
import React, { useState } from 'react'
import { ThemeDefault } from '@react-above/modal-theme-default'
import {
  createModal,
  ModalRenderer,
} from '../src'
const Modal = createModal({
  theme: ThemeDefault(),
})
const Renderer: ModalRenderer = ({ close }) => {
  return (
    <Modal.Surface>
      <Modal.Body>
      </Modal.Body>
    </Modal.Surface>
  )
}
// @filename: node_modules/@types/react/index.d.ts
export = React;
declare namespace React {
    interface ReactElement<P = any, T extends string | JSXElementConstructor<any> = string | JSXElementConstructor<any>> {
    }
    type ReactNode = ReactChild | ReactFragment | ReactPortal | boolean | null | undefined;
    function createContext<T>(
        defaultValue: T,
    ): Context<T>;
    type FC<P = {}> = FunctionComponent<P>;
    interface FunctionComponent<P = {}> {
        (props: PropsWithChildren<P>, context?: any): ReactElement<any, any> | null;
    }
    type ComponentRef<T extends ElementType> = T extends NamedExoticComponent<
    >
            ? Method
            : never;
    interface MutableRefObject<T> {
    }
    function useContext<T>(context: Context<T>/*, (not public API) observedBits?: number|boolean */): T;
    /**
     */
    function useState<S>(initialState: S | (() => S)): [S, Dispatch<SetStateAction<S>>];
    function useRef<T>(initialValue: T): MutableRefObject<T>;
    /**
     */
    function useEffect(effect: EffectCallback, deps?: DependencyList): void;
    interface AriaAttributes {
    }
}
// @filename: src/create-modal.tsx
import React, { FC } from 'react'
import { ModalFC, ModalProps } from './types'
import { ThemeOutput, ThemeProvider, useTheme } from './themes'
export interface CreateModal<TModal extends ModalFC, TOptions = void> {
  theme: ThemeOutput<TModal, TOptions>
}
export function createModal<TModal extends ModalFC, TOptions = void>({
  theme,
}: CreateModal<TModal, TOptions>): TModal {
  const WrappedModal: ModalFC = (props) => (
    <ThemeProvider value={theme}>
    </ThemeProvider>
  )
  return theme.extend(WrappedModal)
}
// @filename: src/index.ts
export { createModal } from './create-modal'
export type {
  ModalRenderer,
} from './types'
// @filename: src/themes/context.ts
import { createContext, useContext } from 'react'
import { ModalFC } from '../types'
import { ThemeOutput } from './types'
type AnyTheme = ThemeOutput<ModalFC, unknown>
const ThemeContext = createContext<AnyTheme>({} as AnyTheme)
export const ThemeProvider = ThemeContext.Provider
export function useTheme() {
}
// @filename: src/themes/index.ts
export * from './context'
export * from './types'
// @filename: src/themes/types.ts
import { A11yAttributes, ModalFC, Refs, ReplaceVoid } from '../types'
export interface ThemeOutput<TModal extends ModalFC, TOptions = void> {
  extend: (modal: ModalFC) => TModal
}
// @filename: src/types/index.ts
export * from './shared'
export * from './util'
// @filename: src/types/shared.ts
import { AriaAttributes, FC, MutableRefObject, ReactNode } from 'react'
import { ChildfreeFC } from './util'
export type Refs = {
}
export interface LifecycleCallbacks {
}
export type ModalRenderer = ChildfreeFC<{ close: () => void }>
export type A11yAttributes = { role: 'dialog' } & AriaAttributes
export type ModalProps = {
} & LifecycleCallbacks
export type ModalFC = FC<ModalProps>
// @filename: src/types/util.ts
import { ReactElement } from 'react'
/**
 */
export type ReplaceVoid<T> = T extends void
  ? Exclude<T | undefined, void> extends never
    ? undefined
    : Exclude<T | undefined, void>
  : T
export type ChildfreeFC<T = {}> = (props: T) => ReactElement<any, any> | null
