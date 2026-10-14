// @noEmit: true
// A signature in a message (TS2658's signatureToString) elides what comes after tsc's approximateLength of 160, like a type: `Promise<...>` (nest's testing tests)
interface InstanceWrapperXXXXXXXX<T> { a: T }
interface ResolutionContextXXXXXX { b: 1 }
declare class ModuleXXXXXXXXXXXXXXX { c: 1 }
declare let target: { m<T>(wrapper: InstanceWrapperXXXXXXXX<T>, collection: Map<string, InstanceWrapperXXXXXXXX<any>>, moduleRef: ModuleXXXXXXXXXXXXXXX, resolutionContext?: ResolutionContextXXXXXX): Promise<void> };
declare let src: { m: { x: 1 } };
target = src;
