const Token = Object.freeze({
  TEXT: 'token-text',
  LINE_NUMBER: 'token-line-number',
  CURSOR: 'token-cursor',
  NEWLINE: 'token-newline',
  ENDLINE: 'token-endline',
});

function addLineStartTokensTo(lineNumbers, indent, tokens) {
  if (lineNumbers) {
    tokens.push({type: Token.LINE_NUMBER});
  }
  if (indent) {
    tokens.push({
      type: Token.TEXT,
      text: indent,
      extraClass: 'token-indent',
    });
  }
}

const textTokenRegex = /([^\r\n]+)|(\r\n|\r|\n)/y;
function tokenizeStringInto(text, offset, extraClass, lineNumbers, indent, tokens) {
  let prevLastIndex = textTokenRegex.lastIndex = 0;
  let match;
  while (match = textTokenRegex.exec(text)) {
    if (match[1]) {
      tokens.push({
        type: Token.TEXT,
        text: match[1],
        offset: offset === null ? null : offset + prevLastIndex,
        extraClass: extraClass,
      });
    } else if (match[2]) {
      tokens.push({
        type: Token.NEWLINE,
        offset: offset === null ? null : offset + prevLastIndex,
        extraClass: extraClass,
      });
      addLineStartTokensTo(lineNumbers, indent, tokens);
    }
    prevLastIndex = textTokenRegex.lastIndex;
  }
}

function applyTokenToElem(token, elem) {
  elem.debugToken = token;
  if (token.offset !== undefined && token.offset !== null) {
    elem.tokenOffset = token.offset;
  }
  if (token.extraClass) {
    elem.classList.add(token.extraClass);
  }
}

function renderTokensTo(parent, tokens) {
  let pendingLine = null;
  let lineNumberTally = 0;
  let addLineNumber = false;

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

      if (addLineNumber) {
        const lineNumberSpan = document.createElement('span');
        lineNumberSpan.classList.add('token-line-number');
        lineNumberTally++;
        lineNumberSpan.textContent = lineNumberTally;
        pendingLine.prepend(lineNumberSpan);
      }

      parent.appendChild(pendingLine);

      pendingLine = null;
      addLineNumber = false;
    }
  }

  while (parent.lastElementChild) {
    parent.removeChild(parent.lastElementChild);
  }

  ensureLine();

  for (const token of tokens) {
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
        div.classList.add('token-cursor');
        applyTokenToElem(token, div);
        ensureLine();
        pendingLine.appendChild(div);
      } break;

      case Token.LINE_NUMBER: {
        addLineNumber = true;
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
    console.error('Token stream did not end with Token.ENDLINE');
    finishLine({});
  }
}
