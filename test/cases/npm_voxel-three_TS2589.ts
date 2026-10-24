// origin: npm voxel-three@0.3.0 https://github.com/tseijp/voxelizer tsconfig.json seed 20261209
// flip: no
// @forceConsistentCasingInFileNames: true
// @ignoreDeprecations: 6.0
// @lib: esnext, dom
// @module: esnext
// @moduleResolution: bundler
// @skipLibCheck: true
// @strict: true
// @target: esnext
// @esModuleInterop: true
// glre (shipped as TypeScript sources) types a Proxy as X<T> over a big recursive node type: tttt reports TS2589
// "excessively deep" on `as unknown as X<T>` (and on the project, a TS2590 at a use in voxel-three), tsgo neither.
// Cut by reduce.py from the project and the package sources.
// @filename: node_modules/glre/package.json
{"name": "glre", "version": "0.54.0", "type": "module", "main": "./dist/index.cjs", "types": "./dist/index.d.ts", "module": "./dist/index.js", "exports": {"./packages.json": "./packages.json", "./src/addons": {"types": "./src/addons/index.ts", "import": "./src/addons/index.ts", "default": "./src/addons/index.ts"}, "./src/buffers": {"types": "./src/buffers/index.ts", "import": "./src/buffers/index.ts", "default": "./src/buffers/index.ts"}, "./src/native": {"types": "./src/native.ts", "import": "./src/native.ts", "default": "./src/native.ts"}, "./src/node": {"types": "./src/node/index.ts", "import": "./src/node/index.ts", "default": "./src/node/index.ts"}, "./src/react": {"types": "./src/react.ts", "import": "./src/react.ts", "default": "./src/react.ts"}, "./src/solid": {"types": "./src/solid.ts", "import": "./src/solid.ts", "default": "./src/solid.ts"}, "./src": {"types": "./src/index.ts", "import": "./src/index.ts", "default": "./src/index.ts"}, "./addons": {"types": "./dist/addons/index.d.ts", "import": {"types": "./dist/addons/index.d.ts", "default": "./dist/addons.js"}, "module": {"types": "./dist/addons/index.d.ts", "default": "./dist/addons.js"}, "require": "./dist/addons.cjs", "default": "./dist/addons.js"}, "./buffers": {"types": "./dist/buffers/index.d.ts", "import": {"types": "./dist/buffers/index.d.ts", "default": "./dist/buffers.js"}, "module": {"types": "./dist/buffers/index.d.ts", "default": "./dist/buffers.js"}, "require": "./dist/buffers.cjs", "default": "./dist/buffers.js"}, "./native": {"types": "./dist/native.d.ts", "import": {"types": "./dist/native.d.ts", "default": "./dist/native.js"}, "module": {"types": "./dist/native.d.ts", "default": "./dist/native.js"}, "require": "./dist/native.cjs", "default": "./dist/native.js"}, "./node": {"types": "./dist/node/index.d.ts", "import": {"types": "./dist/node/index.d.ts", "default": "./dist/node.js"}, "module": {"types": "./dist/node/index.d.ts", "default": "./dist/node.js"}, "require": "./dist/node.cjs", "default": "./dist/node.js"}, "./react": {"types": "./dist/react.d.ts", "import": {"types": "./dist/react.d.ts", "default": "./dist/react.js"}, "module": {"types": "./dist/react.d.ts", "default": "./dist/react.js"}, "require": "./dist/react.cjs", "default": "./dist/react.js"}, "./solid": {"types": "./dist/solid.d.ts", "import": {"types": "./dist/solid.d.ts", "default": "./dist/solid.js"}, "module": {"types": "./dist/solid.d.ts", "default": "./dist/solid.js"}, "require": "./dist/solid.cjs", "default": "./dist/solid.js"}, ".": {"types": "./dist/index.d.ts", "import": {"types": "./dist/index.d.ts", "default": "./dist/index.js"}, "module": {"types": "./dist/index.d.ts", "default": "./dist/index.js"}, "require": "./dist/index.cjs", "default": "./dist/index.js"}, "./*": {"types": "./dist/index.d.ts", "import": {"types": "./dist/index.d.ts", "default": "./dist/index.js"}, "module": {"types": "./dist/index.d.ts", "default": "./dist/index.js"}, "require": "./dist/index.cjs", "default": "./dist/index.js"}}}
// @filename: package.json
{"name": "@voxelizer/root", "version": "0.1.0"}
// @filename: projects/demo/package.json
{"name": "demo", "version": "0.1.0", "type": "module"}
// @filename: node_modules/glre/src/node/create.ts
import { addToScope, assign, toVar } from './scope'
import type { Bool, Constants as C, Functions, NodeProps, NodeTypes, Operators, X, Y } from './types'
export const create = <T extends C>(type: NodeTypes, props?: NodeProps | null, ...children: Y[]) => {
                if (key === 'toVar') return toVar.bind(null, x)
        const x = new Proxy({}, { get, set }) as unknown as X<T>
}
// @filename: node_modules/glre/src/node/index.ts
export * from './scope'
// @filename: node_modules/glre/src/node/scope.ts
import { conversion, create } from './create'
import type { FnLayout, FnType, Constants as C, Int, NodeProps, Struct, StructFactory, StructFields, X, Y } from './types'
export function toVar<T extends StructFields>(x: Struct<T>, id?: string): Struct<T>
// @filename: node_modules/glre/src/node/types.ts
import { CONSTANTS, CONVERSIONS, FUNCTIONS, OPERATOR_KEYS, OPERATOR_TYPE_RULES, SWIZZLE_BASE_MAP, SWIZZLE_RESULT_MAP } from './utils/const'
export type Constants = (typeof CONSTANTS)[number] | 'void'
/**
 */
type _SwizzleLength<A extends string> =
        A extends `${infer _}${infer A}` ? A extends '' ? 1 :
        A extends `${infer _}${infer B}` ? B extends '' ? 2 :
        B extends `${infer _}${infer C}` ? C extends '' ? 3 :
        4 : never : never : never
type _SwizzleBaseMap = typeof SWIZZLE_BASE_MAP
type _SwizzleResultMap = typeof SWIZZLE_RESULT_MAP
type _SwizzleBase<T extends C> = T extends keyof _SwizzleBaseMap ? _SwizzleBaseMap[T] : never
type _SwizzleResult<T extends C, L extends 1 | 2 | 3 | 4> = _SwizzleResultMap[_SwizzleBase<T>][L]
type InferSwizzleType<T extends C, S extends string> = _SwizzleLength<S> extends infer L extends 1 | 2 | 3 | 4 ? _SwizzleResult<_SwizzleBase<T>, L> : never
type InferOperator<L extends C, R extends C> =
        L extends R ? L :
        'float'
type _Swizzles<T extends string> = T | `${T}${T}` | `${T}${T}${T}` | `${T}${T}${T}${T}`
export type Swizzles = _Swizzles<'x' | 'y' | 'z' | 'w'> | _Swizzles<'r' | 'g' | 'b' | 'a'> | _Swizzles<'p' | 'q'> | _Swizzles<'s' | 't'>
export type Void = XImpl<'void'>
export type Bool = XImpl<'bool'>
export type UInt = XImpl<'uint'>
export type Int = XImpl<'int'>
export type Float = XImpl<'float'>
export type BVec2 = XImpl<'bvec2'>
export type IVec2 = XImpl<'ivec2'>
export type UVec2 = XImpl<'uvec2'>
export type Vec2 = XImpl<'vec2'>
export type BVec3 = XImpl<'bvec3'>
export type IVec3 = XImpl<'ivec3'>
export type UVec3 = XImpl<'uvec3'>
export type Vec3 = XImpl<'vec3'>
export type BVec4 = XImpl<'bvec4'>
export type IVec4 = XImpl<'ivec4'>
export type UVec4 = XImpl<'uvec4'>
export type Vec4 = XImpl<'vec4'>
export type Color = XImpl<'color'>
export type Mat2 = XImpl<'mat2'>
export type Mat3 = XImpl<'mat3'>
export type Mat4 = XImpl<'mat4'>
export type Texture = XImpl<'texture'>
export type Sampler2D = XImpl<'sampler2D'>
export type StructBase = XImpl<'struct'>
export type Struct<T extends StructFields = any> = Omit<StructBase, keyof T> & {
        [K in keyof T]: T[K] extends X<infer U> ? X<U> : never
} & {
        toVar(id?: string): Struct<T>
}
export interface ConstantsToType {
        void: Void
        bool: Bool
        uint: UInt
        int: Int
        float: Float
        bvec2: BVec2
        ivec2: IVec2
        uvec2: UVec2
        vec2: Vec2
        bvec3: BVec3
        ivec3: IVec3
        uvec3: UVec3
        vec3: Vec3
        bvec4: BVec4
        ivec4: IVec4
        uvec4: UVec4
        vec4: Vec4
        color: Color
        mat2: Mat2
        mat3: Mat3
        mat4: Mat4
        texture: Texture
        sampler2D: Sampler2D
        struct: StructBase
}
/**
 */
type XImpl<T extends C> = _X<T> & {
        [K in Swizzles]: X<InferSwizzleType<T, K>>
}
export type X<T extends C = C> = T extends keyof ConstantsToType ? ConstantsToType[T] : _X<T>
interface _X<T extends C> {
        toVar(name?: string): X<T>
        add<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<InferOperator<T, U>>
        sub<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<InferOperator<T, U>>
        mul<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<InferOperator<T, U>>
        div<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<InferOperator<T, U>>
        mod<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<InferOperator<T, U>>
        equal<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): Bool
        lessThanEqual<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): Bool
        greaterThan<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): Bool
        greaterThanEqual<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): Bool
        shiftLeft<U extends C>(x: X<U>): X<InferOperator<T, U>>
        shiftRight<U extends C>(x: X<U>): X<InferOperator<T, U>>
        addAssign<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<T>
        subAssign<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<T>
        mulAssign<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<T>
        divAssign<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<T>
        modAssign<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<T>
        bitAndAssign<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<T>
        bitOrAssign<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<T>
        bitXorAssign<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<T>
        shiftLeftAssign<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<T>
        shiftRightAssign<U extends C>(x: ValidateOperator<T, U> extends 0 ? never : number | X<U>): X<T>
        atan2<U extends C>(x: number | X<U>): X<T>
        clamp<U extends C>(min: number | X<U>, max: number | X<U>): X<InferOperator<T, U>>
        max<U extends C>(y: number | X<U>): X<InferOperator<T, U>>
        min<U extends C>(y: number | X<U>): X<InferOperator<T, U>>
        mix<U extends C>(y: number | X<U>, a: number | Float | X<U>): X<InferOperator<T, U>>
        pow<U extends C>(y: number | X<U>): X<T>
        fma<U extends C>(b: number | X<U>, c: number | X<U>): X<T>
        smoothstep<U extends C>(edge0: number | X<U>, edge1: number | X<U>): X<InferOperator<T, U>>
        step<U extends C>(edge: number | X<U>): X<InferOperator<T, U>>
}
// @filename: node_modules/glre/src/node/utils/const.ts
export const TYPE_MAPPING = {
        bool: 'bool',
        uint: 'u32',
        int: 'i32',
        float: 'f32',
        bvec2: 'vec2<bool>',
        ivec2: 'vec2i',
        uvec2: 'vec2u',
        vec2: 'vec2f',
        bvec3: 'vec3<bool>',
        ivec3: 'vec3i',
        uvec3: 'vec3u',
        vec3: 'vec3f',
        bvec4: 'vec4<bool>',
        ivec4: 'vec4i',
        uvec4: 'vec4u',
        vec4: 'vec4f',
} as const
export const CONSTANTS = Object.keys(TYPE_MAPPING) as (keyof typeof TYPE_MAPPING)[]
export const SWIZZLE_BASE_MAP = {
        float: 'float',
        vec2: 'float',
        vec3: 'float',
        vec4: 'float',
        int: 'int',
        ivec2: 'int',
        ivec3: 'int',
        ivec4: 'int',
        uint: 'uint',
        uvec2: 'uint',
        uvec3: 'uint',
        uvec4: 'uint',
        bool: 'bool',
        bvec2: 'bool',
        bvec3: 'bool',
        bvec4: 'bool',
} as const
export const SWIZZLE_RESULT_MAP = {
        float: { 1: 'float', 2: 'vec2', 3: 'vec3', 4: 'vec4', 9: 'mat3', 16: 'mat4' } as const,
        int: { 1: 'int', 2: 'ivec2', 3: 'ivec3', 4: 'ivec4' } as const,
        uint: { 1: 'uint', 2: 'uvec2', 3: 'uvec3', 4: 'uvec4' } as const,
        bool: { 1: 'bool', 2: 'bvec2', 3: 'bvec3', 4: 'bvec4' } as const,
}
// @filename: projects/demo/main.ts
import type { Float, UInt, UVec2, UVec3, Vec3 } from 'glre/src/node'
