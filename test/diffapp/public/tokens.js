const Token = Object.freeze({
  TEXT: 'token-text',
  LINE_NUMBER: 'token-line-number',
  CURSOR: 'token-cursor',
  BUTTON: 'token-button',
  NEWLINE: 'token-newline',
  ENDLINE: 'token-endline',
});

// Options sent to these functions may be mutated by the functions. They may contain:
// - lineNumbers: bool, add line numbers to start of lines
// - indent: string, start each line with given text
// - classes: array of strings, classes to add to final element
// - editorStart: number, start character in editor buffer the text maps to
// - editorEnd: number, max position in editor buffer the text maps to (bounded only by textContent length if undefined)

function makeCursorToken(cursorPosition) {
  return {
    type: Token.CURSOR,
    editorStart: cursorPosition,
    editorEnd: cursorPosition,
  };
}

function makeEndlineToken(editorPosition) {
  return {
    type: Token.ENDLINE,
    editorStart: editorPosition,
    editorEnd: editorPosition,
  }
}

const singleLineRegex = /([^\r\n]*)([\r\n]*)/;
// Takes an array of annotation lines which includes newlines
function tokenizeAnnotationInto(annotationLines, tokens, options) {
  for (const line of annotationLines) {
    const match = singleLineRegex.exec(line);
    if (match) {
      if (match[1]) {
        tokens.push({
          type: Token.TEXT,
          text: match[1],
          classes: options.classes,
        });
      }
      if (match[2]) {
        tokens.push({
          type: Token.TEXT,
          text: match[2].replace('\n', '\\n').replace('\r', '\\r'),
          classes:  [...(options.classes ?? []), 'token-line-end'],
        });
      }
      if (match[1] || match[2]) {
        tokens.push({
          type: Token.NEWLINE,
          classes: options.classes,
        });
      }
    }
  }
}

// See top of file for options
function addLineStartTokensTo(tokens, options) {
  if (options.lineNumbers) {
    tokens.push({
      type: Token.LINE_NUMBER,
      editorStart: options.editorStart,
      editorEnd: options.editorStart,
    });
  }
  if (options.indent) {
    tokens.push({
      type: Token.TEXT,
      text: options.indent,
      editorStart: options.editorStart,
      editorEnd: options.editorStart,
      classes: ['token-indent'],
    });
  }
}

// Turn text into tokens and push them to the tokens array. See top of file for options
const textTokenRegex = /([^\r\n]+)|(\r\n|\r|\n)/y;
function tokenizeStringInto(text, tokens, options) {
  const initialEditorStart = options.editorStart;
  const initialEditorEnd = options.editorEnd ?? Infinity;
  textTokenRegex.lastIndex = 0;
  let match;
  while (match = textTokenRegex.exec(text)) {
    if (match[1]) {
      tokens.push({
        type: Token.TEXT,
        text: match[1],
        editorStart: options.editorStart,
        editorEnd: Math.min(options.editorEnd + match[1].length, initialEditorEnd),
        classes: options.classes,
      });
    } else if (match[2]) {
      tokens.push({
        type: Token.NEWLINE,
        editorStart: options.editorStart,
        editorEnd: options.editorStart,
        classes: options.classes,
      });
    }
    if (initialEditorStart !== undefined) {
      options.editorStart = initialEditorStart + textTokenRegex.lastIndex;
      options.editorEnd = options.editorStart;
    }
    if (match[2]) {
      addLineStartTokensTo(tokens, options);
    }
  }
}

const stderrTokenRegex = /(?<beforeLink>[^\r\n\u001b]* )(?<valgrindLink>\w+ \((?<filename>[\w\./]+\.[ch]):(?<lineNum>\d+)\))|(?<gccLink>(?<filename>[\w\./]+\.[ch]):(?<lineNum>\d+):(?<charNum>\d+):)|(?<normalLine>[^\r\n\u001b]+)|(\u001b(\[(?<escapeCode>[\d;]+)m))|(?<newline>\r\n|\r|\n)/y;
function tokenizeStderrInto(text, tokens) {
  let match;
  let color = null;
  while (match = stderrTokenRegex.exec(text)) {
    if (match.groups.normalLine) {
      tokens.push({
        type: Token.TEXT,
        text: match.groups.normalLine,
        color: color,
      });
    } else if (match.groups.valgrindLink) {
      if (match.groups.beforeLink) {
        tokens.push({
          type: Token.TEXT,
          text: match.groups.beforeLink,
          color: color,
        });
      }
      const filename = match.groups.filename;
      const lineNum = match.groups.lineNum;
      tokens.push({
        type: Token.BUTTON,
        text: match.groups.valgrindLink,
        onclick: () => nativeEditorJumpTo(filename, lineNum, null),
        color: color,
      });
    } else if (match.groups.gccLink) {
      const filename = match.groups.filename;
      const lineNum = match.groups.lineNum;
      const charNum = match.groups.charNum;
      tokens.push({
        type: Token.BUTTON,
        text: match.groups.gccLink,
        onclick: () => nativeEditorJumpTo(filename, lineNum, charNum),
        color: color,
      });
    } else if (match.groups.newline) {
      tokens.push({
        type: Token.NEWLINE,
      });
    } else if (match.groups.escapeCode) {
      for (const code of match.groups.escapeCode.split(';')) {
        switch (code) {
          case '0': color = null; break;
          case '31': color = '#ff3020'; break;
          case '94': color = '#4060ff'; break;
          case '1': break;
          default: console.log('Unkown BASH color code ' + code);
        }
      }
    }
  }
}

function renderTokensTo(parent, tokens) {
  let pendingLine = null;
  let lineNumberTally = 0;
  let lineNumberToken = null;
  let lastEditorEnd = undefined;

  function applyTokenToElem(token, elem) {
    if (token.editorStart !== undefined) {
      elem.editorStart = token.editorStart;
      if (token.editorEnd !== undefined) {
        elem.editorEnd = token.editorEnd;
      }
    } else {
      elem.editorStart = lastEditorEnd;
      elem.editorEnd = lastEditorEnd;
    }
    if (Array.isArray(token.classes)) {
      for (const c of token.classes) {
        elem.classList.add(c);
      }
    }
    if (token.onclick) {
      elem.onclick = token.onclick;
    }
    if (token.color) {
      elem.style.color = token.color;
    }
  }

  function ensureLine() {
    if (!pendingLine) {
      pendingLine = document.createElement('div')
      pendingLine.classList.add('code-line');
    }
  }

  function finishLine(token) {
    if (pendingLine) {
      const div = document.createElement('div');
      div.classList.add('token-line-cap');
      applyTokenToElem(token, div);
      pendingLine.appendChild(div);

      if (lineNumberToken) {
        const lineNumberSpan = document.createElement('span');
        lineNumberSpan.classList.add('token-line-number');
        lineNumberTally++;
        lineNumberSpan.textContent = lineNumberTally;
        applyTokenToElem(lineNumberToken, lineNumberSpan);
        pendingLine.prepend(lineNumberSpan);
      }

      parent.appendChild(pendingLine);

      pendingLine = null;
      lineNumberToken = null;
    }
  }

  while (parent.lastElementChild) {
    parent.removeChild(parent.lastElementChild);
  }

  ensureLine();

  for (const token of tokens) {
    if (token.editorEnd !== undefined) {
      lastEditorEnd = token.editorEnd;
    }

    switch (token.type) {

      case Token.TEXT: {
        const span = document.createElement('span');
        span.textContent = token.text;
        applyTokenToElem(token, span);
        ensureLine();
        pendingLine.appendChild(span);
      } break;

      case Token.CURSOR: {
        const div = document.createElement('div');
        div.classList.add(token.type);
        applyTokenToElem(token, div);
        ensureLine();
        pendingLine.appendChild(div);
      } break;

      case Token.LINE_NUMBER: {
        lineNumberToken = token;
      } break;

      case Token.BUTTON: {
        const button = document.createElement('button');
        button.classList.add(token.type);
        button.textContent = token.text;
        applyTokenToElem(token, button);
        pendingLine.appendChild(button);
      } break;

      case Token.NEWLINE: {
        finishLine(token);
        ensureLine();
      } break;

      case Token.ENDLINE: {
        finishLine(token);
      } break;
    }
  }

  if (pendingLine) {
    finishLine({});
  }
}
