// JSON files parse like tsc's parseJsonText: one array per '[', an object literal with '{' expected for anything that isn't a value, and validateJsonValue's errors without parseErrorAtPosition's deduplication
// @module: commonjs
// @resolveJsonModule: true
// @Filename: main.ts
import j0 = require('./j0.json');
import j1 = require('./j1.json');
import j2 = require('./j2.json');
import j3 = require('./j3.json');
import j4 = require('./j4.json');
import j5 = require('./j5.json');
import j6 = require('./j6.json');
import j7 = require('./j7.json');
import j8 = require('./j8.json');
import j9 = require('./j9.json');
import j10 = require('./j10.json');
import j11 = require('./j11.json');
import j12 = require('./j12.json');
import j13 = require('./j13.json');
// @Filename: j0.json
"a": 1, "b": [true]
// @Filename: j1.json
[1] [2]
// @Filename: j2.json
-1
// @Filename: j3.json
foo
// @Filename: j4.json
}
// @Filename: j5.json
1 2
// @Filename: j6.json
{"a": 1} x
// @Filename: j7.json
- "x"
// @Filename: j8.json
's'
// @Filename: j9.json
null true
// @Filename: j10.json
"a" "b": 2
// @Filename: j11.json
]
// @Filename: j12.json
-a: 1
// @Filename: j13.json
{"x"}
