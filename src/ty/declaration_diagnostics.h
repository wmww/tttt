#pragma once

#include "prelude.h"

typedef struct SourceFile SourceFile;
typedef struct ErrorCtx ErrorCtx;

// The diagnostics tsc's declaration transformer reports (tsgo's transformers/declarations with the node builder's
// pseudo types, pseudo.h), without emitting anything: the isolatedDeclarations ones (TS9007-TS9039) and what the
// symbol tracker reports (TS4xxx). Run by the checker that checked the file, after checking it (plan 40)

// tsc's getEmitDeclarations (declaration or composite) and the file is one tsc emits (getSourceFilesToEmit): not a
// declaration file, JSON or JavaScript
bool declaration_diagnostics_wanted(SourceFile const* file);
// `emit`: also write the file's declarations (SourceFile.declaration_output, plan 64)
void declaration_diagnostics_check(SourceFile const* file, ErrorCtx* errs, bool emit);
