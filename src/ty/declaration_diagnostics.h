#pragma once

#include "prelude.h"

typedef struct SourceFile SourceFile;
typedef struct ErrorCtx ErrorCtx;

// The diagnostics tsc's declaration transformer reports (transformers/declarations.ts with the syntactic type builder of
// expressionToTypeNode.ts), without emitting anything: so far the isolatedDeclarations ones (TS9007-TS9038). Run by the
// checker that checked the file, after checking it (plan 40)

// tsc's getEmitDeclarations (declaration or composite) and the file is one tsc emits (getSourceFilesToEmit): not a
// declaration file, JSON or JavaScript
bool declaration_diagnostics_wanted(SourceFile const* file);
void declaration_diagnostics_check(SourceFile const* file, ErrorCtx* errs);
