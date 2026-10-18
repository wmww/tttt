// @strict: true
// An unresolved type name doesn't count towards the printed type's approximate length (tsc's error type with the name as
// its alias, printed by TypeAlias.ToTypeReferenceNode): the long name doesn't make the printer elide onCommit
interface W {
  p?: {
      checkDCE: (fn: unknown) => void;
      supportsFiber: boolean;
      supportsFlight: boolean;
      renderers: Map<number, RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR>;
      hasUnsupportedRendererAttached: boolean;
      onCommit: (
      ) => void;
    };
}
interface W {
  p?: {
    };
}
