// exactOptionalPropertyTypes: writing to a property removes the missing type only if the property is optional
// (tsc's getFlowTypeOfAccessExpression): `r.current = cb` with `current: T` and T inferred as `(() => void) | missing`
// from a destructured optional prop (excalidraw TTDDialogInput.tsx:23 under eopt) is fine
// @strict: true
// @exactOptionalPropertyTypes: true
interface MutableRefObject<T> { current: T }
interface RefObject<T> { current: T }
declare function useRef<T>(initialValue: T): MutableRefObject<T>;
declare function useRef<T>(initialValue: T | null): RefObject<T | null>;
declare function useRef<T>(initialValue: T | undefined): RefObject<T | undefined>;
interface P { cb?: () => void }
export const f = ({ cb }: P) => {
  const r = useRef(cb);
  r.current = cb;
  r.zz;
  const g = id(cb);
  g.zz;
};
export const h = (p: P) => {
  const r = useRef(p.cb);
  r.zz;
};
declare function id<T>(x: T | undefined): T;
