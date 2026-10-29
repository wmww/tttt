// @target: es2015
// A module specifier that isn't a string literal (tsc's parseModuleSpecifier takes any expression) is TS1141 at its
// error span (getErrorSpanForNode): a class expression's name. Seed 20261117 --syntax, classAbstractManyKeywords
// drop-modifier
import class D {}
import (1 + 2);
