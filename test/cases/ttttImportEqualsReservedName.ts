// An import-equals module reference is an entity name without reserved words, after a dot too (tsc's
// parseModuleReference: parseEntityName(allowReservedWords false), parseRightSideOfDot then parseIdentifier): the word
// is TS1359 and left for what follows. Babel's regression/keyword-qualified-type-disallowed fixture
declare namespace Y { const a: number }
import X = Y.if;
import A = this.A;
