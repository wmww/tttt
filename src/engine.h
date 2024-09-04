#pragma once

typedef struct SourceFile SourceFile;
typedef struct arc_js_object ArcObject;

void run_in_engine(SourceFile const* scope, ArcObject root);
