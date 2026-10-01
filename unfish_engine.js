/**
 * 🐡 Unfish Browser Engine, Parser, Bytecode VM & Blocks System (v2.0)
 * Comprehensive, zero-dependency browser runtime for the Unfish Programming Language.
 * Supports:
 *  - Lexical scanner & tokenization (Tokens Inspector)
 *  - Pratt expression & recursive descent AST parser (AST Inspector)
 *  - Bytecode chunk compiler & disassembler (Disasm Inspector)
 *  - Full language interpreter with lexical scopes, closures, structs, enums, pattern matching,
 *    higher-order functions, exceptions (try/catch/finally), and standard library builtins
 *  - Step debugger engine with breakpoints, call stack, and variable inspection
 *  - Bidirectional visual block generator (Blocks <-> AST <-> Unfish Text)
 */

(function(global) {
  'use strict';

  // =========================================================================
  // 1. LEXER & TOKENIZER
  // =========================================================================

  const KEYWORDS = {
    'let': 'TOKEN_LET',
    'say': 'TOKEN_SAY',
    'print': 'TOKEN_PRINT',
    'function': 'TOKEN_FUNCTION',
    'fn': 'TOKEN_FN',
    'return': 'TOKEN_RETURN',
    'if': 'TOKEN_IF',
    'else': 'TOKEN_ELSE',
    'while': 'TOKEN_WHILE',
    'for': 'TOKEN_FOR',
    'in': 'TOKEN_IN',
    'repeat': 'TOKEN_REPEAT',
    'times': 'TOKEN_TIMES',
    'break': 'TOKEN_BREAK',
    'continue': 'TOKEN_CONTINUE',
    'true': 'TOKEN_TRUE',
    'false': 'TOKEN_FALSE',
    'null': 'TOKEN_NULL',
    'and': 'TOKEN_AND',
    'or': 'TOKEN_OR',
    'not': 'TOKEN_NOT',
    'try': 'TOKEN_TRY',
    'catch': 'TOKEN_CATCH',
    'finally': 'TOKEN_FINALLY',
    'struct': 'TOKEN_STRUCT',
    'enum': 'TOKEN_ENUM',
    'match': 'TOKEN_MATCH',
    'when': 'TOKEN_WHEN',
    'case': 'TOKEN_WHEN',
    'import': 'TOKEN_IMPORT',
    'from': 'TOKEN_FROM',
    'as': 'TOKEN_AS',
    'spawn': 'TOKEN_SPAWN',
    'yield': 'TOKEN_YIELD',
    'async': 'TOKEN_ASYNC',
    'await': 'TOKEN_AWAIT',
    'trait': 'TOKEN_TRAIT',
    'impl': 'TOKEN_IMPL'
  };

  class Token {
    constructor(type, value, line, column) {
      this.type = type;
      this.value = value;
      this.line = line;
      this.column = column;
    }
    toString() {
      return `${this.type.padEnd(16)} '${this.value}' (${this.line}:${this.column})`;
    }
  }

  class Lexer {
    constructor(source) {
      this.source = source || '';
      this.length = this.source.length;
      this.cursor = 0;
      this.line = 1;
      this.col = 1;
      this.tokens = [];
      this.errors = [];
      this.indentStack = [0];
    }

    peek(offset = 0) {
      const idx = this.cursor + offset;
      return idx < this.length ? this.source[idx] : '\0';
    }

    advance() {
      if (this.cursor >= this.length) return '\0';
      const c = this.source[this.cursor++];
      if (c === '\n') {
        this.line++;
        this.col = 1;
      } else {
        this.col++;
      }
      return c;
    }

    match(expected) {
      if (this.peek() === expected) {
        this.advance();
        return true;
      }
      return false;
    }

    tokenize() {
      let isLineStart = true;
      let parenDepth = 0;

      while (this.cursor < this.length) {
        // Handle indentation at start of line
        if (isLineStart && parenDepth === 0) {
          let indent = 0;
          let blankLine = false;

          while (this.cursor < this.length) {
            const c = this.peek();
            if (c === ' ') {
              indent++;
              this.advance();
            } else if (c === '\t') {
              indent += 4;
              this.advance();
            } else if (c === '\r') {
              this.advance();
            } else if (c === '\n') {
              this.advance();
              indent = 0; // blank line
            } else if (c === '#') {
              // Comment line
              while (this.cursor < this.length && this.peek() !== '\n') {
                this.advance();
              }
              if (this.peek() === '\n') {
                this.advance();
                indent = 0;
              }
            } else {
              break;
            }
          }

          if (this.cursor >= this.length) break;

          const currentIndent = this.indentStack[this.indentStack.length - 1];
          if (indent > currentIndent) {
            this.indentStack.push(indent);
            this.tokens.push(new Token('TOKEN_INDENT', `${indent}`, this.line, 1));
          } else if (indent < currentIndent) {
            while (this.indentStack.length > 1 && this.indentStack[this.indentStack.length - 1] > indent) {
              this.indentStack.pop();
              this.tokens.push(new Token('TOKEN_DEDENT', '', this.line, 1));
            }
          }
          isLineStart = false;
        }

        const startLine = this.line;
        const startCol = this.col;
        const c = this.advance();

        if (c === ' ' || c === '\t' || c === '\r') {
          continue;
        }

        if (c === '\n') {
          if (parenDepth > 0) continue;
          this.tokens.push(new Token('TOKEN_NEWLINE', '\\n', startLine, startCol));
          isLineStart = true;
          continue;
        }

        if (c === '#') {
          while (this.cursor < this.length && this.peek() !== '\n') {
            this.advance();
          }
          continue;
        }

        // Numbers
        if (/[0-9]/.test(c)) {
          let numStr = c;
          if (c === '0' && (this.peek() === 'x' || this.peek() === 'X')) {
            numStr += this.advance();
            while (/[0-9a-fA-F]/.test(this.peek())) {
              numStr += this.advance();
            }
          } else {
            while (/[0-9]/.test(this.peek())) {
              numStr += this.advance();
            }
            if (this.peek() === '.' && /[0-9]/.test(this.peek(1))) {
              numStr += this.advance(); // '.'
              while (/[0-9]/.test(this.peek())) {
                numStr += this.advance();
              }
            }
          }
          this.tokens.push(new Token('TOKEN_NUMBER', numStr, startLine, startCol));
          continue;
        }

        // Strings (double quote, single quote, interpolated f"...", triple quoted """...""")
        if (c === 'f' && (this.peek() === '"' || this.peek() === "'")) {
          const quote = this.advance();
          let str = '';
          while (this.cursor < this.length && this.peek() !== quote) {
            if (this.peek() === '\\') {
              this.advance();
              str += '\\' + this.advance();
            } else {
              str += this.advance();
            }
          }
          if (this.peek() === quote) this.advance();
          this.tokens.push(new Token('TOKEN_INTERP_STRING', str, startLine, startCol));
          continue;
        }

        if (c === '"' || c === "'") {
          // Triple quoted multiline string check
          if (c === '"' && this.peek() === '"' && this.peek(1) === '"') {
            this.advance(); // 2nd quote
            this.advance(); // 3rd quote
            let str = '';
            while (this.cursor < this.length) {
              if (this.peek() === '"' && this.peek(1) === '"' && this.peek(2) === '"') {
                this.advance();
                this.advance();
                this.advance();
                break;
              }
              str += this.advance();
            }
            this.tokens.push(new Token('TOKEN_STRING', str, startLine, startCol));
            continue;
          }

          let str = '';
          while (this.cursor < this.length && this.peek() !== c) {
            if (this.peek() === '\\') {
              this.advance();
              const esc = this.advance();
              if (esc === 'n') str += '\n';
              else if (esc === 't') str += '\t';
              else if (esc === 'r') str += '\r';
              else if (esc === '\\') str += '\\';
              else if (esc === '"') str += '"';
              else if (esc === "'") str += "'";
              else str += esc;
            } else {
              str += this.advance();
            }
          }
          if (this.peek() === c) this.advance();
          this.tokens.push(new Token('TOKEN_STRING', str, startLine, startCol));
          continue;
        }

        // Identifiers and Keywords
        if (/[a-zA-Z_]/.test(c)) {
          let id = c;
          while (/[a-zA-Z0-9_]/.test(this.peek())) {
            id += this.advance();
          }
          const kwType = KEYWORDS[id];
          if (kwType) {
            this.tokens.push(new Token(kwType, id, startLine, startCol));
          } else {
            this.tokens.push(new Token('TOKEN_IDENTIFIER', id, startLine, startCol));
          }
          continue;
        }

        // Operators & Punctuation
        switch (c) {
          case '(': parenDepth++; this.tokens.push(new Token('TOKEN_LPAREN', '(', startLine, startCol)); break;
          case ')': if (parenDepth > 0) parenDepth--; this.tokens.push(new Token('TOKEN_RPAREN', ')', startLine, startCol)); break;
          case '[': parenDepth++; this.tokens.push(new Token('TOKEN_LBRACKET', '[', startLine, startCol)); break;
          case ']': if (parenDepth > 0) parenDepth--; this.tokens.push(new Token('TOKEN_RBRACKET', ']', startLine, startCol)); break;
          case '{': parenDepth++; this.tokens.push(new Token('TOKEN_LBRACE', '{', startLine, startCol)); break;
          case '}': if (parenDepth > 0) parenDepth--; this.tokens.push(new Token('TOKEN_RBRACE', '}', startLine, startCol)); break;
          case ':': this.tokens.push(new Token('TOKEN_COLON', ':', startLine, startCol)); break;
          case ',': this.tokens.push(new Token('TOKEN_COMMA', ',', startLine, startCol)); break;
          case '.':
            if (this.peek() === '.' && this.peek(1) === '.') {
              this.advance();
              this.advance();
              this.tokens.push(new Token('TOKEN_DOT_DOT_DOT', '...', startLine, startCol));
            } else {
              this.tokens.push(new Token('TOKEN_DOT', '.', startLine, startCol));
            }
            break;
          case '+': this.tokens.push(new Token('TOKEN_PLUS', '+', startLine, startCol)); break;
          case '-':
            if (this.match('>')) {
              this.tokens.push(new Token('TOKEN_ARROW', '->', startLine, startCol));
            } else {
              this.tokens.push(new Token('TOKEN_MINUS', '-', startLine, startCol));
            }
            break;
          case '*': this.tokens.push(new Token('TOKEN_STAR', '*', startLine, startCol)); break;
          case '/': this.tokens.push(new Token('TOKEN_SLASH', '/', startLine, startCol)); break;
          case '%': this.tokens.push(new Token('TOKEN_PERCENT', '%', startLine, startCol)); break;
          case '^': this.tokens.push(new Token('TOKEN_CARET', '^', startLine, startCol)); break;
          case '=':
            if (this.match('=')) {
              this.tokens.push(new Token('TOKEN_EQUAL_EQUAL', '==', startLine, startCol));
            } else {
              this.tokens.push(new Token('TOKEN_EQUAL', '=', startLine, startCol));
            }
            break;
          case '!':
            if (this.match('=')) {
              this.tokens.push(new Token('TOKEN_BANG_EQUAL', '!=', startLine, startCol));
            } else {
              this.tokens.push(new Token('TOKEN_NOT', '!', startLine, startCol));
            }
            break;
          case '<':
            if (this.match('=')) {
              this.tokens.push(new Token('TOKEN_LESS_EQUAL', '<=', startLine, startCol));
            } else {
              this.tokens.push(new Token('TOKEN_LESS', '<', startLine, startCol));
            }
            break;
          case '>':
            if (this.match('=')) {
              this.tokens.push(new Token('TOKEN_GREATER_EQUAL', '>=', startLine, startCol));
            } else {
              this.tokens.push(new Token('TOKEN_GREATER', '>', startLine, startCol));
            }
            break;
          case '|':
            if (this.match('>')) {
              this.tokens.push(new Token('TOKEN_PIPE_FORWARD', '|>', startLine, startCol));
            } else {
              this.tokens.push(new Token('TOKEN_PIPE', '|', startLine, startCol));
            }
            break;
          default:
            this.errors.push(`Line ${startLine}:${startCol} Unexpected character: '${c}'`);
            break;
        }
      }

      // Close remaining indents
      while (this.indentStack.length > 1) {
        this.indentStack.pop();
        this.tokens.push(new Token('TOKEN_DEDENT', '', this.line, this.col));
      }

      this.tokens.push(new Token('TOKEN_EOF', '', this.line, this.col));
      return { tokens: this.tokens, errors: this.errors };
    }
  }

  // =========================================================================
  // 2. ABSTRACT SYNTAX TREE (AST) & PARSER
  // =========================================================================

  class Parser {
    constructor(tokens) {
      this.tokens = tokens || [];
      this.current = 0;
      this.errors = [];
    }

    peek() {
      return this.tokens[this.current] || new Token('TOKEN_EOF', '', 0, 0);
    }

    previous() {
      return this.tokens[this.current - 1];
    }

    isAtEnd() {
      return this.peek().type === 'TOKEN_EOF';
    }

    advance() {
      if (!this.isAtEnd()) this.current++;
      return this.previous();
    }

    check(type) {
      if (this.isAtEnd()) return false;
      return this.peek().type === type;
    }

    match(...types) {
      for (const type of types) {
        if (this.check(type)) {
          this.advance();
          return true;
        }
      }
      return false;
    }

    consume(type, message) {
      if (this.check(type)) return this.advance();
      const p = this.peek();
      const err = `Line ${p.line}:${p.column} Syntax Error: ${message} (found '${p.value || p.type}')`;
      this.errors.push(err);
      throw new Error(err);
    }

    skipNewlines() {
      while (this.match('TOKEN_NEWLINE')) {}
    }

    parse() {
      const statements = [];
      this.skipNewlines();
      while (!this.isAtEnd()) {
        try {
          const stmt = this.declaration();
          if (stmt) statements.push(stmt);
        } catch (e) {
          this.synchronize();
        }
        this.skipNewlines();
      }
      return {
        type: 'Program',
        body: statements,
        errors: this.errors
      };
    }

    synchronize() {
      this.advance();
      while (!this.isAtEnd()) {
        if (this.previous().type === 'TOKEN_NEWLINE') return;
        switch (this.peek().type) {
          case 'TOKEN_LET':
          case 'TOKEN_FUNCTION':
          case 'TOKEN_FN':
          case 'TOKEN_IF':
          case 'TOKEN_WHILE':
          case 'TOKEN_FOR':
          case 'TOKEN_REPEAT':
          case 'TOKEN_RETURN':
          case 'TOKEN_SAY':
          case 'TOKEN_TRY':
          case 'TOKEN_STRUCT':
          case 'TOKEN_ENUM':
          case 'TOKEN_MATCH':
            return;
        }
        this.advance();
      }
    }

    declaration() {
      this.skipNewlines();
      if (this.match('TOKEN_LET')) return this.letDeclaration();
      if (this.match('TOKEN_FUNCTION') || this.match('TOKEN_FN')) return this.functionDeclaration();
      if (this.match('TOKEN_STRUCT')) return this.structDeclaration();
      if (this.match('TOKEN_ENUM')) return this.enumDeclaration();
      return this.statement();
    }

    letDeclaration() {
      // Check for destructuring: let [a, b] = ... or let {x, y} = ...
      if (this.match('TOKEN_LBRACKET')) {
        const elements = [];
        let restName = null;
        if (!this.check('TOKEN_RBRACKET')) {
          do {
            if (this.match('TOKEN_DOT_DOT_DOT')) {
              restName = this.consume('TOKEN_IDENTIFIER', "Expected identifier after '...'").value;
              break;
            }
            elements.push(this.consume('TOKEN_IDENTIFIER', "Expected variable name in array destructure").value);
          } while (this.match('TOKEN_COMMA'));
        }
        this.consume('TOKEN_RBRACKET', "Expected ']' in array destructure");
        this.consume('TOKEN_EQUAL', "Expected '=' after let destructure pattern");
        const init = this.expression();
        return { type: 'LetArrayDestructure', elements, restName, init };
      }

      if (this.match('TOKEN_LBRACE')) {
        const keys = [];
        if (!this.check('TOKEN_RBRACE')) {
          do {
            keys.push(this.consume('TOKEN_IDENTIFIER', "Expected key name in map destructure").value);
          } while (this.match('TOKEN_COMMA'));
        }
        this.consume('TOKEN_RBRACE', "Expected '}' in map destructure");
        this.consume('TOKEN_EQUAL', "Expected '=' after let destructure pattern");
        const init = this.expression();
        return { type: 'LetMapDestructure', keys, init };
      }

      const nameToken = this.consume('TOKEN_IDENTIFIER', "Expected variable name after 'let'");
      let init = null;
      if (this.match('TOKEN_EQUAL')) {
        init = this.expression();
      }
      return { type: 'LetStatement', name: nameToken.value, init };
    }

    functionDeclaration() {
      const nameToken = this.consume('TOKEN_IDENTIFIER', "Expected function name");
      this.consume('TOKEN_LPAREN', "Expected '(' after function name");
      const params = [];
      const defaults = {};
      if (!this.check('TOKEN_RPAREN')) {
        do {
          const param = this.consume('TOKEN_IDENTIFIER', "Expected parameter name").value;
          params.push(param);
          if (this.match('TOKEN_EQUAL')) {
            defaults[param] = this.expression();
          }
        } while (this.match('TOKEN_COMMA'));
      }
      this.consume('TOKEN_RPAREN', "Expected ')' after parameters");
      this.consume('TOKEN_COLON', "Expected ':' before function body");
      const body = this.block();
      return { type: 'FunctionDeclaration', name: nameToken.value, params, defaults, body };
    }

    structDeclaration() {
      const name = this.consume('TOKEN_IDENTIFIER', "Expected struct name").value;
      this.consume('TOKEN_COLON', "Expected ':' after struct name");
      this.skipNewlines();
      this.consume('TOKEN_INDENT', "Expected indented block for struct fields and methods");
      const fields = [];
      const methods = [];

      while (!this.check('TOKEN_DEDENT') && !this.isAtEnd()) {
        this.skipNewlines();
        if (this.check('TOKEN_DEDENT')) break;

        if (this.match('TOKEN_FUNCTION') || this.match('TOKEN_FN')) {
          const methodName = this.consume('TOKEN_IDENTIFIER', "Expected method name").value;
          this.consume('TOKEN_LPAREN', "Expected '(' in method declaration");
          const params = [];
          if (!this.check('TOKEN_RPAREN')) {
            do {
              params.push(this.consume('TOKEN_IDENTIFIER', "Expected parameter name").value);
            } while (this.match('TOKEN_COMMA'));
          }
          this.consume('TOKEN_RPAREN', "Expected ')' after parameters");
          this.consume('TOKEN_COLON', "Expected ':' before method body");
          const body = this.block();
          methods.push({ type: 'Method', name: methodName, params, body });
        } else if (this.check('TOKEN_IDENTIFIER')) {
          fields.push(this.advance().value);
          this.match('TOKEN_NEWLINE');
        } else {
          this.advance();
        }
      }
      this.match('TOKEN_DEDENT');
      return { type: 'StructDeclaration', name, fields, methods };
    }

    enumDeclaration() {
      const name = this.consume('TOKEN_IDENTIFIER', "Expected enum name").value;
      this.consume('TOKEN_COLON', "Expected ':' after enum name");
      this.skipNewlines();
      this.consume('TOKEN_INDENT', "Expected indented block for enum variants");
      const variants = [];

      while (!this.check('TOKEN_DEDENT') && !this.isAtEnd()) {
        this.skipNewlines();
        if (this.check('TOKEN_DEDENT')) break;

        if (this.check('TOKEN_IDENTIFIER')) {
          const varName = this.advance().value;
          const params = [];
          if (this.match('TOKEN_LPAREN')) {
            if (!this.check('TOKEN_RPAREN')) {
              do {
                params.push(this.consume('TOKEN_IDENTIFIER', "Expected parameter name").value);
              } while (this.match('TOKEN_COMMA'));
            }
            this.consume('TOKEN_RPAREN', "Expected ')' after variant parameters");
          }
          variants.push({ name: varName, params });
          this.match('TOKEN_NEWLINE');
        } else {
          this.advance();
        }
      }
      this.match('TOKEN_DEDENT');
      return { type: 'EnumDeclaration', name, variants };
    }

    statement() {
      if (this.match('TOKEN_SAY')) {
        const expr = this.expression();
        return { type: 'SayStatement', expr };
      }
      if (this.match('TOKEN_PRINT')) {
        const expr = this.expression();
        return { type: 'PrintStatement', expr };
      }
      if (this.match('TOKEN_RETURN')) {
        let val = null;
        if (!this.check('TOKEN_NEWLINE') && !this.check('TOKEN_DEDENT') && !this.isAtEnd()) {
          val = this.expression();
        }
        return { type: 'ReturnStatement', value: val };
      }
      if (this.match('TOKEN_IF')) return this.ifStatement();
      if (this.match('TOKEN_WHILE')) return this.whileStatement();
      if (this.match('TOKEN_FOR')) return this.forStatement();
      if (this.match('TOKEN_REPEAT')) return this.repeatStatement();
      if (this.match('TOKEN_BREAK')) return { type: 'BreakStatement' };
      if (this.match('TOKEN_CONTINUE')) return { type: 'ContinueStatement' };
      if (this.match('TOKEN_TRY')) return this.tryStatement();
      if (this.match('TOKEN_MATCH')) return this.matchStatement();

      return this.expressionStatement();
    }

    ifStatement() {
      const condition = this.expression();
      this.consume('TOKEN_COLON', "Expected ':' after if condition");
      const thenBranch = this.block();
      let elseBranch = null;

      this.skipNewlines();
      if (this.match('TOKEN_ELSE')) {
        if (this.match('TOKEN_IF')) {
          elseBranch = this.ifStatement();
        } else {
          this.consume('TOKEN_COLON', "Expected ':' after else");
          elseBranch = this.block();
        }
      }
      return { type: 'IfStatement', condition, thenBranch, elseBranch };
    }

    whileStatement() {
      const condition = this.expression();
      this.consume('TOKEN_COLON', "Expected ':' after while condition");
      const body = this.block();
      return { type: 'WhileStatement', condition, body };
    }

    forStatement() {
      const iter = this.consume('TOKEN_IDENTIFIER', "Expected iterator variable name").value;
      this.consume('TOKEN_IN', "Expected 'in' after for iterator");
      const iterable = this.expression();
      this.consume('TOKEN_COLON', "Expected ':' after for expression");
      const body = this.block();
      return { type: 'ForStatement', iterator: iter, iterable, body };
    }

    repeatStatement() {
      const count = this.expression();
      this.consume('TOKEN_TIMES', "Expected 'times' after repeat count");
      this.match('TOKEN_COLON');
      const body = this.block();
      return { type: 'RepeatStatement', count, body };
    }

    tryStatement() {
      this.consume('TOKEN_COLON', "Expected ':' after try");
      const tryBlock = this.block();
      let catchVar = null;
      let catchBlock = null;
      let finallyBlock = null;

      this.skipNewlines();
      if (this.match('TOKEN_CATCH')) {
        if (this.check('TOKEN_IDENTIFIER')) {
          catchVar = this.advance().value;
        }
        this.consume('TOKEN_COLON', "Expected ':' after catch");
        catchBlock = this.block();
      }

      this.skipNewlines();
      if (this.match('TOKEN_FINALLY')) {
        this.consume('TOKEN_COLON', "Expected ':' after finally");
        finallyBlock = this.block();
      }

      return { type: 'TryStatement', tryBlock, catchVar, catchBlock, finallyBlock };
    }

    matchStatement() {
      const target = this.expression();
      this.consume('TOKEN_COLON', "Expected ':' after match target");
      this.skipNewlines();
      this.consume('TOKEN_INDENT', "Expected indented block of when cases");
      const cases = [];

      while (!this.check('TOKEN_DEDENT') && !this.isAtEnd()) {
        this.skipNewlines();
        if (this.check('TOKEN_DEDENT')) break;

        if (this.match('TOKEN_WHEN')) {
          let pattern = null;
          if (this.match('TOKEN_IDENTIFIER')) {
            const id = this.previous().value;
            if (this.match('TOKEN_DOT') && this.check('TOKEN_IDENTIFIER')) {
              const subId = this.advance().value;
              const params = [];
              if (this.match('TOKEN_LPAREN')) {
                if (!this.check('TOKEN_RPAREN')) {
                  do {
                    params.push(this.consume('TOKEN_IDENTIFIER', "Expected identifier in pattern").value);
                  } while (this.match('TOKEN_COMMA'));
                }
                this.consume('TOKEN_RPAREN', "Expected ')' in pattern");
              }
              pattern = { type: 'EnumPattern', enumName: id, variant: subId, params };
            } else if (this.match('TOKEN_LPAREN')) {
              const params = [];
              if (!this.check('TOKEN_RPAREN')) {
                do {
                  params.push(this.consume('TOKEN_IDENTIFIER', "Expected identifier in pattern").value);
                } while (this.match('TOKEN_COMMA'));
              }
              this.consume('TOKEN_RPAREN', "Expected ')' in pattern");
              pattern = { type: 'VariantPattern', variant: id, params };
            } else {
              pattern = { type: 'IdentifierPattern', name: id };
            }
          } else {
            pattern = { type: 'LiteralPattern', value: this.primary() };
          }

          this.consume('TOKEN_COLON', "Expected ':' after when pattern");
          const body = this.block();
          cases.push({ pattern, body });
        } else {
          this.advance();
        }
      }
      this.match('TOKEN_DEDENT');
      return { type: 'MatchStatement', target, cases };
    }

    block() {
      this.skipNewlines();
      if (this.match('TOKEN_INDENT')) {
        const stmts = [];
        while (!this.check('TOKEN_DEDENT') && !this.isAtEnd()) {
          this.skipNewlines();
          if (this.check('TOKEN_DEDENT')) break;
          const s = this.declaration();
          if (s) stmts.push(s);
          this.skipNewlines();
        }
        this.consume('TOKEN_DEDENT', "Expected dedent at end of block");
        return { type: 'Block', statements: stmts };
      }
      // Single line block
      const s = this.statement();
      return { type: 'Block', statements: s ? [s] : [] };
    }

    expressionStatement() {
      const expr = this.expression();
      if (this.match('TOKEN_EQUAL')) {
        const val = this.expression();
        return { type: 'AssignStatement', target: expr, value: val };
      }
      return { type: 'ExpressionStatement', expression: expr };
    }

    expression() {
      return this.pipe();
    }

    pipe() {
      let expr = this.logicalOr();
      while (this.match('TOKEN_PIPE_FORWARD')) {
        const right = this.logicalOr();
        expr = { type: 'PipeExpression', left: expr, right };
      }
      return expr;
    }

    logicalOr() {
      let expr = this.logicalAnd();
      while (this.match('TOKEN_OR')) {
        const right = this.logicalAnd();
        expr = { type: 'BinaryExpression', operator: 'or', left: expr, right };
      }
      return expr;
    }

    logicalAnd() {
      let expr = this.equality();
      while (this.match('TOKEN_AND')) {
        const right = this.equality();
        expr = { type: 'BinaryExpression', operator: 'and', left: expr, right };
      }
      return expr;
    }

    equality() {
      let expr = this.comparison();
      while (this.match('TOKEN_EQUAL_EQUAL', 'TOKEN_BANG_EQUAL')) {
        const op = this.previous().value;
        const right = this.comparison();
        expr = { type: 'BinaryExpression', operator: op, left: expr, right };
      }
      return expr;
    }

    comparison() {
      let expr = this.term();
      while (this.match('TOKEN_GREATER', 'TOKEN_GREATER_EQUAL', 'TOKEN_LESS', 'TOKEN_LESS_EQUAL')) {
        const op = this.previous().value;
        const right = this.term();
        expr = { type: 'BinaryExpression', operator: op, left: expr, right };
      }
      return expr;
    }

    term() {
      let expr = this.factor();
      while (this.match('TOKEN_PLUS', 'TOKEN_MINUS')) {
        const op = this.previous().value;
        const right = this.factor();
        expr = { type: 'BinaryExpression', operator: op, left: expr, right };
      }
      return expr;
    }

    factor() {
      let expr = this.power();
      while (this.match('TOKEN_STAR', 'TOKEN_SLASH', 'TOKEN_PERCENT')) {
        const op = this.previous().value;
        const right = this.power();
        expr = { type: 'BinaryExpression', operator: op, left: expr, right };
      }
      return expr;
    }

    power() {
      let expr = this.unary();
      while (this.match('TOKEN_CARET')) {
        const right = this.unary();
        expr = { type: 'BinaryExpression', operator: '^', left: expr, right };
      }
      return expr;
    }

    unary() {
      if (this.match('TOKEN_NOT', 'TOKEN_MINUS')) {
        const op = this.previous().value;
        const right = this.unary();
        return { type: 'UnaryExpression', operator: op, right };
      }
      return this.call();
    }

    call() {
      let expr = this.primary();

      while (true) {
        if (this.match('TOKEN_LPAREN')) {
          const args = [];
          if (!this.check('TOKEN_RPAREN')) {
            do {
              if (this.match('TOKEN_DOT_DOT_DOT')) {
                args.push({ type: 'Spread', expr: this.expression() });
              } else {
                args.push(this.expression());
              }
            } while (this.match('TOKEN_COMMA'));
          }
          this.consume('TOKEN_RPAREN', "Expected ')' after arguments");
          expr = { type: 'CallExpression', callee: expr, args };
        } else if (this.match('TOKEN_DOT')) {
          const name = this.consume('TOKEN_IDENTIFIER', "Expected property name after '.'").value;
          expr = { type: 'MemberExpression', object: expr, property: name };
        } else if (this.match('TOKEN_LBRACKET')) {
          const index = this.expression();
          this.consume('TOKEN_RBRACKET', "Expected ']' after index");
          expr = { type: 'IndexExpression', object: expr, index };
        } else {
          break;
        }
      }
      return expr;
    }

    primary() {
      if (this.match('TOKEN_FALSE')) return { type: 'Literal', value: false };
      if (this.match('TOKEN_TRUE')) return { type: 'Literal', value: true };
      if (this.match('TOKEN_NULL')) return { type: 'Literal', value: null };

      if (this.match('TOKEN_NUMBER')) {
        const raw = this.previous().value;
        const val = raw.startsWith('0x') || raw.startsWith('0X') ? parseInt(raw, 16) : parseFloat(raw);
        return { type: 'Literal', value: val };
      }

      if (this.match('TOKEN_STRING')) {
        return { type: 'Literal', value: this.previous().value };
      }

      if (this.match('TOKEN_INTERP_STRING')) {
        return { type: 'InterpolatedString', raw: this.previous().value };
      }

      if (this.match('TOKEN_IDENTIFIER') || this.match('TOKEN_SPAWN') || this.match('TOKEN_YIELD')) {
        return { type: 'Identifier', name: this.previous().value };
      }

      // Anonymous function / Lambda: fn(x): x * 2 or fn(x): return x * 2
      if (this.match('TOKEN_FN') || this.match('TOKEN_FUNCTION')) {
        this.consume('TOKEN_LPAREN', "Expected '(' after fn");
        const params = [];
        if (!this.check('TOKEN_RPAREN')) {
          do {
            params.push(this.consume('TOKEN_IDENTIFIER', "Expected param").value);
          } while (this.match('TOKEN_COMMA'));
        }
        this.consume('TOKEN_RPAREN', "Expected ')'");
        this.consume('TOKEN_COLON', "Expected ':' before lambda body");
        this.skipNewlines();
        if (this.check('TOKEN_INDENT')) {
          const body = this.block();
          return { type: 'Lambda', params, body };
        }
        if (this.match('TOKEN_RETURN')) {
          const retExpr = this.expression();
          return { type: 'Lambda', params, bodyExpr: retExpr };
        }
        const bodyExpr = this.expression();
        return { type: 'Lambda', params, bodyExpr };
      }

      // Arrays: [1, 2, 3] or spread [...a]
      if (this.match('TOKEN_LBRACKET')) {
        const elements = [];
        if (!this.check('TOKEN_RBRACKET')) {
          do {
            if (this.match('TOKEN_DOT_DOT_DOT')) {
              elements.push({ type: 'Spread', expr: this.expression() });
            } else {
              elements.push(this.expression());
            }
          } while (this.match('TOKEN_COMMA'));
        }
        this.consume('TOKEN_RBRACKET', "Expected ']' at end of array");
        return { type: 'ArrayLiteral', elements };
      }

      // Maps: {"a": 1, "b": 2}
      if (this.match('TOKEN_LBRACE')) {
        const entries = [];
        if (!this.check('TOKEN_RBRACE')) {
          do {
            let key = null;
            if (this.check('TOKEN_STRING') || this.check('TOKEN_IDENTIFIER')) {
              key = this.advance().value;
            } else {
              this.consume('TOKEN_STRING', "Expected map key string or identifier");
            }
            this.consume('TOKEN_COLON', "Expected ':' after map key");
            const val = this.expression();
            entries.push({ key, val });
          } while (this.match('TOKEN_COMMA'));
        }
        this.consume('TOKEN_RBRACE', "Expected '}' at end of map");
        return { type: 'MapLiteral', entries };
      }

      // Grouping: (expr)
      if (this.match('TOKEN_LPAREN')) {
        const expr = this.expression();
        this.consume('TOKEN_RPAREN', "Expected ')' after expression");
        return expr;
      }

      const p = this.peek();
      throw new Error(`Line ${p.line}:${p.column} Unexpected token in expression: '${p.value || p.type}'`);
    }
  }

  // =========================================================================
  // 3. BYTECODE COMPILER & DISASSEMBLER SIMULATION
  // =========================================================================

  class Disassembler {
    static disassemble(ast) {
      if (!ast || !ast.body) return '; Empty AST';
      const lines = [
        '; == UNFISH BYTECODE DISASSEMBLY (42 OPCODES ISA) ==',
        '; Format: [Offset]  Opcode                 Operands / Constant',
        ''
      ];

      let offset = 0;
      const formatOp = (op, arg = '') => {
        const str = `0x${offset.toString(16).padStart(4, '0')}   ${op.padEnd(22)} ${arg}`;
        offset += 2;
        return str;
      };

      const walk = (node) => {
        if (!node) return;
        switch (node.type) {
          case 'LetStatement':
            if (node.init) walk(node.init);
            lines.push(formatOp('OP_SET_GLOBAL', `${node.name}`));
            lines.push(formatOp('OP_POP'));
            break;
          case 'AssignStatement':
            walk(node.value);
            if (node.target.type === 'Identifier') {
              lines.push(formatOp('OP_SET_GLOBAL', `${node.target.name}`));
            } else if (node.target.type === 'MemberExpression') {
              walk(node.target.object);
              lines.push(formatOp('OP_SET_PROPERTY', `.${node.target.property}`));
            } else if (node.target.type === 'IndexExpression') {
              walk(node.target.object);
              walk(node.target.index);
              lines.push(formatOp('OP_SET_INDEX'));
            }
            break;
          case 'SayStatement':
            walk(node.expr);
            lines.push(formatOp('OP_SAY'));
            break;
          case 'PrintStatement':
            walk(node.expr);
            lines.push(formatOp('OP_PRINT'));
            break;
          case 'Literal':
            lines.push(formatOp('OP_CONSTANT', JSON.stringify(node.value)));
            break;
          case 'Identifier':
            lines.push(formatOp('OP_GET_GLOBAL', `${node.name}`));
            break;
          case 'BinaryExpression':
            walk(node.left);
            walk(node.right);
            switch (node.operator) {
              case '+': lines.push(formatOp('OP_ADD')); break;
              case '-': lines.push(formatOp('OP_SUB')); break;
              case '*': lines.push(formatOp('OP_MUL')); break;
              case '/': lines.push(formatOp('OP_DIV')); break;
              case '%': lines.push(formatOp('OP_MOD')); break;
              case '^': lines.push(formatOp('OP_POW')); break;
              case '==': lines.push(formatOp('OP_EQUAL')); break;
              case '!=': lines.push(formatOp('OP_NOT_EQUAL')); break;
              case '<': lines.push(formatOp('OP_LESS')); break;
              case '<=': lines.push(formatOp('OP_LESS_EQUAL')); break;
              case '>': lines.push(formatOp('OP_GREATER')); break;
              case '>=': lines.push(formatOp('OP_GREATER_EQUAL')); break;
              default: lines.push(formatOp(`OP_${node.operator.toUpperCase()}`)); break;
            }
            break;
          case 'UnaryExpression':
            walk(node.right);
            if (node.operator === '-') lines.push(formatOp('OP_NEGATE'));
            if (node.operator === 'not' || node.operator === '!') lines.push(formatOp('OP_NOT'));
            break;
          case 'CallExpression':
            walk(node.callee);
            for (const arg of node.args) {
              walk(arg);
            }
            lines.push(formatOp('OP_CALL', `argc=${node.args.length}`));
            break;
          case 'FunctionDeclaration':
            lines.push(formatOp('OP_CLOSURE', `fn ${node.name}(${node.params.join(', ')})`));
            lines.push(formatOp('OP_SET_GLOBAL', `${node.name}`));
            break;
          case 'IfStatement':
            walk(node.condition);
            lines.push(formatOp('OP_JUMP_IF_FALSE', `-> +then_len`));
            walk(node.thenBranch);
            if (node.elseBranch) {
              lines.push(formatOp('OP_JUMP', `-> +else_len`));
              walk(node.elseBranch);
            }
            break;
          case 'WhileStatement':
            lines.push(formatOp('; loop_header'));
            walk(node.condition);
            lines.push(formatOp('OP_JUMP_IF_FALSE', `-> +body_len`));
            walk(node.body);
            lines.push(formatOp('OP_LOOP', `-> -header`));
            break;
          case 'ArrayLiteral':
            for (const el of node.elements) walk(el);
            lines.push(formatOp('OP_ARRAY', `len=${node.elements.length}`));
            break;
          case 'MapLiteral':
            for (const ent of node.entries) {
              lines.push(formatOp('OP_CONSTANT', JSON.stringify(ent.key)));
              walk(ent.val);
            }
            lines.push(formatOp('OP_MAP', `count=${node.entries.length}`));
            break;
          case 'Block':
            for (const s of node.statements) walk(s);
            break;
          default:
            lines.push(formatOp(`OP_${node.type.toUpperCase()}`));
            break;
        }
      };

      for (const stmt of ast.body) {
        walk(stmt);
      }
      lines.push(formatOp('OP_RETURN'));
      return lines.join('\n');
    }
  }

  // =========================================================================
  // 4. INTERPRETER & RUNTIME ENVIRONMENT
  // =========================================================================

  class Environment {
    constructor(parent = null) {
      this.values = {};
      this.parent = parent;
    }

    define(name, value) {
      this.values[name] = value;
      return value;
    }

    assign(name, value) {
      if (Object.prototype.hasOwnProperty.call(this.values, name)) {
        this.values[name] = value;
        return value;
      }
      if (this.parent) {
        return this.parent.assign(name, value);
      }
      this.values[name] = value;
      return value;
    }

    get(name) {
      if (Object.prototype.hasOwnProperty.call(this.values, name)) {
        return this.values[name];
      }
      if (this.parent) {
        return this.parent.get(name);
      }
      return undefined;
    }
  }

  class ReturnException {
    constructor(value) {
      this.value = value;
    }
  }

  class UnfishRuntimeError extends Error {
    constructor(message, kind = 'RuntimeError') {
      super(message);
      this.kind = kind;
    }
  }

  class Interpreter {
    constructor(outputCallback = null) {
      this.outputCallback = outputCallback;
      this.output = [];
      this.errors = [];
      this.stepCount = 0;
      this.maxSteps = 1000000;
      this.callStack = [];
      this.breakpoints = new Set();
      this.globalEnv = new Environment();
      this.currentEnv = this.globalEnv;
      this.initBuiltins();
    }

    initBuiltins() {
      const env = this.globalEnv;

      // Core builtins
      env.define('say', (val) => {
        const s = this.stringify(val);
        this.output.push(s);
        if (this.outputCallback) this.outputCallback(s);
        return null;
      });

      env.define('print', (val) => {
        const s = this.stringify(val);
        if (this.output.length === 0) this.output.push('');
        this.output[this.output.length - 1] += s;
        return null;
      });

      env.define('len', (val) => {
        if (val === null || val === undefined) return 0;
        if (typeof val === 'string' || Array.isArray(val)) return val.length;
        if (val instanceof Uint8Array) return val.length;
        if (typeof val === 'object') return Object.keys(val).length;
        return 0;
      });

      env.define('type_of', (val) => {
        if (val === null || val === undefined) return 'null';
        if (typeof val === 'boolean') return 'boolean';
        if (typeof val === 'number') return 'number';
        if (typeof val === 'string') return 'string';
        if (Array.isArray(val)) return 'array';
        if (typeof val === 'function') return 'function';
        if (val instanceof Uint8Array) return 'buffer';
        if (val && val.__struct) return 'instance';
        if (val && val.__enum) return 'enum_val';
        if (val && val.__channel) return 'channel';
        if (val && val.__fiber) return 'fiber';
        return 'map';
      });

      env.define('to_string', (v) => this.stringify(v));
      env.define('to_number', (v) => {
        const n = Number(v);
        return isNaN(n) ? null : n;
      });

      // Array builtins
      env.define('push', (arr, val) => {
        if (Array.isArray(arr)) arr.push(val);
        return null;
      });

      env.define('pop', (arr) => {
        if (Array.isArray(arr) && arr.length > 0) return arr.pop();
        return null;
      });

      env.define('range', (a, b, step = 1) => {
        let start = 0, end = a;
        if (b !== undefined) {
          start = a;
          end = b;
        }
        const res = [];
        for (let i = start; step > 0 ? i < end : i > end; i += step) {
          if (res.length >= 100000) break;
          res.push(i);
        }
        return res;
      });

      env.define('map', (arr, fn) => Array.isArray(arr) ? arr.map((item, idx) => fn(item, idx)) : []);
      env.define('filter', (arr, fn) => Array.isArray(arr) ? arr.filter((item, idx) => fn(item, idx)) : []);
      env.define('reduce', (arr, fn, init) => {
        if (!Array.isArray(arr)) return null;
        if (init !== undefined) return arr.reduce((acc, item) => fn(acc, item), init);
        return arr.reduce((acc, item) => fn(acc, item));
      });

      env.define('sort', (arr, cmp) => {
        if (!Array.isArray(arr)) return [];
        const copy = [...arr];
        if (cmp) copy.sort((a, b) => cmp(a, b));
        else copy.sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
        return copy;
      });

      env.define('reverse', (arr) => Array.isArray(arr) ? [...arr].reverse() : []);
      env.define('concat', (a, b) => (Array.isArray(a) && Array.isArray(b)) ? a.concat(b) : []);
      env.define('flatten', (arr) => Array.isArray(arr) ? arr.flat(1) : []);
      env.define('fill', (arr, val) => {
        if (Array.isArray(arr)) arr.fill(val);
        return arr;
      });
      env.define('zip', (a, b) => {
        if (!Array.isArray(a) || !Array.isArray(b)) return [];
        const minLen = Math.min(a.length, b.length);
        const res = [];
        for (let i = 0; i < minLen; i++) res.push([a[i], b[i]]);
        return res;
      });

      // Map builtins
      env.define('keys', (m) => (m && typeof m === 'object') ? Object.keys(m) : []);
      env.define('values', (m) => (m && typeof m === 'object') ? Object.values(m) : []);
      env.define('has_key', (m, k) => (m && typeof m === 'object') ? Object.prototype.hasOwnProperty.call(m, k) : false);
      env.define('delete', (m, k) => {
        if (m && typeof m === 'object') {
          const had = Object.prototype.hasOwnProperty.call(m, k);
          delete m[k];
          return had;
        }
        return false;
      });

      // String builtins
      env.define('split', (s, delim) => typeof s === 'string' ? s.split(delim) : []);
      env.define('join', (arr, sep = '') => Array.isArray(arr) ? arr.map(this.stringify).join(sep) : '');
      env.define('trim', (s) => typeof s === 'string' ? s.trim() : '');
      env.define('trim_start', (s) => typeof s === 'string' ? s.trimStart() : '');
      env.define('trim_end', (s) => typeof s === 'string' ? s.trimEnd() : '');
      env.define('pad_start', (s, len, pad = ' ') => typeof s === 'string' ? s.padStart(len, pad) : '');
      env.define('pad_end', (s, len, pad = ' ') => typeof s === 'string' ? s.padEnd(len, pad) : '');
      env.define('chars', (s) => typeof s === 'string' ? Array.from(s) : []);
      env.define('count', (s, sub) => {
        if (typeof s !== 'string' || typeof sub !== 'string' || sub.length === 0) return 0;
        return s.split(sub).length - 1;
      });
      env.define('replace', (s, oldStr, newStr) => typeof s === 'string' ? s.split(oldStr).join(newStr) : '');
      env.define('to_upper', (s) => typeof s === 'string' ? s.toUpperCase() : '');
      env.define('to_lower', (s) => typeof s === 'string' ? s.toLowerCase() : '');
      env.define('contains', (s, sub) => typeof s === 'string' ? s.includes(sub) : false);
      env.define('starts_with', (s, pfx) => typeof s === 'string' ? s.startsWith(pfx) : false);
      env.define('ends_with', (s, sfx) => typeof s === 'string' ? s.endsWith(sfx) : false);
      env.define('char_at', (s, idx) => {
        if (typeof s !== 'string') return '';
        const i = idx < 0 ? s.length + idx : idx;
        return (i >= 0 && i < s.length) ? s[i] : '';
      });
      env.define('substring', (s, start, end) => {
        if (typeof s !== 'string') return '';
        const len = s.length;
        let st = start < 0 ? len + start : start;
        let en = end === undefined ? len : (end < 0 ? len + end : end);
        return s.substring(Math.max(0, st), Math.max(0, en));
      });
      env.define('index_of', (s, sub) => typeof s === 'string' ? s.indexOf(sub) : -1);

      // Math
      env.define('PI', Math.PI);
      env.define('E', Math.E);
      env.define('abs', Math.abs);
      env.define('floor', Math.floor);
      env.define('ceil', Math.ceil);
      env.define('round', Math.round);
      env.define('sqrt', Math.sqrt);
      env.define('pow', Math.pow);
      env.define('min', (a, b) => Math.min(a, b));
      env.define('max', (a, b) => Math.max(a, b));
      env.define('sin', Math.sin);
      env.define('cos', Math.cos);
      env.define('tan', Math.tan);
      env.define('random', Math.random);
      env.define('random_int', (min, max) => Math.floor(Math.random() * (max - min + 1)) + min);

      // Buffer
      env.define('buffer', (size) => new Uint8Array(Math.max(0, Math.floor(size))));
      env.define('buffer_size', (buf) => buf instanceof Uint8Array ? buf.length : 0);
      env.define('buffer_get', (buf, off) => (buf instanceof Uint8Array && off >= 0 && off < buf.length) ? buf[off] : 0);
      env.define('buffer_set', (buf, off, byte) => {
        if (buf instanceof Uint8Array && off >= 0 && off < buf.length) {
          buf[off] = byte & 0xff;
        }
        return null;
      });

      // Concurrency: Fibers and Channels
      const spawnedFibers = [];
      env.define('channel', (cap = 16) => ({
        __channel: true,
        buffer: [],
        closed: false,
        capacity: Number(cap) || 16
      }));
      env.define('close_channel', (ch) => {
        if (ch && ch.__channel) ch.closed = true;
        return null;
      });
      env.define('send', (ch, val) => {
        if (ch && ch.__channel && !ch.closed) {
          ch.buffer.push(val);
        }
        return null;
      });
      env.define('recv', (ch) => {
        if (ch && ch.__channel && ch.buffer.length > 0) {
          return ch.buffer.shift();
        }
        return null;
      });
      env.define('spawn', (fn, ...args) => {
        const fiber = {
          __fiber: true,
          fn: fn,
          args: args
        };
        spawnedFibers.push(fiber);
        return fiber;
      });
      env.define('yield', (val) => val);
      env.define('run_scheduler', () => {
        let count = 0;
        while (spawnedFibers.length > 0) {
          const fiber = spawnedFibers.shift();
          if (typeof fiber.fn === 'function') {
            fiber.fn(...fiber.args);
            count++;
          }
        }
        return count;
      });

      // System / Introspection
      env.define('clock', () => performance.now() / 1000.0);
      env.define('assert', (cond, msg) => {
        if (!cond) throw new UnfishRuntimeError(msg || 'AssertionError', 'AssertionError');
        return null;
      });
      env.define('error', (msg, kind) => {
        throw new UnfishRuntimeError(msg, kind || 'UserError');
      });
      env.define('inspect', (val) => ({
        type: env.get('type_of')(val),
        length: val && val.length !== undefined ? val.length : undefined,
        fields: val && typeof val === 'object' ? Object.keys(val) : []
      }));

      // Modules: sys, fs, time, json
      const sysMod = {
        platform: () => 'web-browser',
        exit: (code) => { throw new UnfishRuntimeError(`Process exited with code ${code}`, 'Exit'); },
        args: () => ['unfish_studio'],
        cwd: () => '/workspace',
        env: () => null,
        set_env: () => false,
        exec: () => -1
      };
      env.define('sys', sysMod);

      const fsStore = {};
      const fsMod = {
        read_text: (p) => fsStore[p] || null,
        write_text: (p, c) => { fsStore[p] = String(c); return true; },
        append_text: (p, c) => { fsStore[p] = (fsStore[p] || '') + String(c); return true; },
        exists: (p) => Object.prototype.hasOwnProperty.call(fsStore, p),
        delete_file: (p) => {
          if (Object.prototype.hasOwnProperty.call(fsStore, p)) {
            delete fsStore[p];
            return true;
          }
          return false;
        },
        list_dir: () => Object.keys(fsStore),
        mkdir: () => true,
        remove_dir: () => true,
        is_file: (p) => Object.prototype.hasOwnProperty.call(fsStore, p),
        is_dir: () => false,
        file_size: (p) => fsStore[p] ? fsStore[p].length : null
      };
      env.define('fs', fsMod);

      const timeMod = {
        clock: () => performance.now() / 1000.0,
        sleep: () => null,
        timestamp: () => Math.floor(Date.now() / 1000.0),
        format: (ts) => new Date(ts * 1000).toLocaleString(),
        iso: (ts) => new Date(ts * 1000).toISOString()
      };
      env.define('time', timeMod);

      const jsonMod = {
        parse: (str) => {
          try { return JSON.parse(str); } catch (e) { return null; }
        },
        stringify: (v) => JSON.stringify(v, null, 2)
      };
      env.define('json', jsonMod);
    }

    stringify(val) {
      if (val === null || val === undefined) return 'null';
      if (typeof val === 'boolean') return val ? 'true' : 'false';
      if (typeof val === 'number') return String(val);
      if (typeof val === 'string') return val;
      if (Array.isArray(val)) {
        return '[' + val.map((x) => this.stringify(x)).join(', ') + ']';
      }
      if (val instanceof Uint8Array) {
        return `<Buffer size=${val.length}>`;
      }
      if (typeof val === 'function') return '<function>';
      if (val.__struct) {
        const fields = Object.keys(val).filter((k) => k !== '__struct' && k !== '__methods');
        return `${val.__struct}(${fields.map((f) => `${f}=${this.stringify(val[f])}`).join(', ')})`;
      }
      if (val.__enum) {
        const args = val.__args ? `(${val.__args.map((a) => this.stringify(a)).join(', ')})` : '';
        return `${val.__enum}.${val.__variant}${args}`;
      }
      if (typeof val === 'object') {
        const entries = Object.entries(val).map(([k, v]) => `"${k}": ${this.stringify(v)}`);
        return '{' + entries.join(', ') + '}';
      }
      return String(val);
    }

    execute(ast) {
      this.stepCount = 0;
      this.output = [];
      this.errors = [];
      try {
        if (ast && ast.body) {
          for (const stmt of ast.body) {
            this.executeStatement(stmt, this.globalEnv);
          }
        }
        return {
          exit_code: 0,
          stdout: this.output.join('\n') + (this.output.length > 0 ? '\n' : ''),
          stderr: ''
        };
      } catch (err) {
        if (err.kind === 'Exit') {
          return {
            exit_code: 0,
            stdout: this.output.join('\n') + (this.output.length > 0 ? '\n' : ''),
            stderr: ''
          };
        }
        const errMsg = err.message || String(err);
        this.errors.push(errMsg);
        return {
          exit_code: 1,
          stdout: this.output.join('\n') + (this.output.length > 0 ? '\n' : ''),
          stderr: errMsg
        };
      }
    }

    checkSteps() {
      if (++this.stepCount > this.maxSteps) {
        throw new UnfishRuntimeError('Execution limit exceeded (infinite loop or deep recursion protection)');
      }
    }

    executeStatement(stmt, env) {
      this.checkSteps();
      if (!stmt) return null;

      switch (stmt.type) {
        case 'LetStatement': {
          const val = stmt.init ? this.evalExpr(stmt.init, env) : null;
          env.define(stmt.name, val);
          return null;
        }
        case 'LetArrayDestructure': {
          const arr = this.evalExpr(stmt.init, env);
          if (Array.isArray(arr)) {
            stmt.elements.forEach((name, i) => env.define(name, arr[i] !== undefined ? arr[i] : null));
            if (stmt.restName) env.define(stmt.restName, arr.slice(stmt.elements.length));
          }
          return null;
        }
        case 'LetMapDestructure': {
          const map = this.evalExpr(stmt.init, env);
          if (map && typeof map === 'object') {
            stmt.keys.forEach((k) => env.define(k, map[k] !== undefined ? map[k] : null));
          }
          return null;
        }
        case 'AssignStatement': {
          const val = this.evalExpr(stmt.value, env);
          if (stmt.target.type === 'Identifier') {
            env.assign(stmt.target.name, val);
          } else if (stmt.target.type === 'MemberExpression') {
            const obj = this.evalExpr(stmt.target.object, env);
            if (obj && typeof obj === 'object') obj[stmt.target.property] = val;
          } else if (stmt.target.type === 'IndexExpression') {
            const obj = this.evalExpr(stmt.target.object, env);
            const idx = this.evalExpr(stmt.target.index, env);
            if (Array.isArray(obj) && typeof idx === 'number') {
              const realIdx = idx < 0 ? obj.length + idx : idx;
              obj[realIdx] = val;
            } else if (obj && typeof obj === 'object') {
              obj[idx] = val;
            }
          }
          return null;
        }
        case 'SayStatement': {
          const val = this.evalExpr(stmt.expr, env);
          const s = this.stringify(val);
          this.output.push(s);
          if (this.outputCallback) this.outputCallback(s);
          return null;
        }
        case 'PrintStatement': {
          const val = this.evalExpr(stmt.expr, env);
          const s = this.stringify(val);
          if (this.output.length === 0) this.output.push('');
          this.output[this.output.length - 1] += s;
          return null;
        }
        case 'ReturnStatement': {
          const val = stmt.value ? this.evalExpr(stmt.value, env) : null;
          throw new ReturnException(val);
        }
        case 'IfStatement': {
          const cond = this.evalExpr(stmt.condition, env);
          if (this.isTruthy(cond)) {
            this.executeBlock(stmt.thenBranch, new Environment(env));
          } else if (stmt.elseBranch) {
            if (stmt.elseBranch.type === 'IfStatement') {
              this.executeStatement(stmt.elseBranch, env);
            } else {
              this.executeBlock(stmt.elseBranch, new Environment(env));
            }
          }
          return null;
        }
        case 'WhileStatement': {
          while (this.isTruthy(this.evalExpr(stmt.condition, env))) {
            this.checkSteps();
            try {
              this.executeBlock(stmt.body, new Environment(env));
            } catch (e) {
              if (e === 'break') break;
              if (e === 'continue') continue;
              throw e;
            }
          }
          return null;
        }
        case 'ForStatement': {
          const iterVal = this.evalExpr(stmt.iterable, env);
          const items = Array.isArray(iterVal) ? iterVal : (typeof iterVal === 'string' ? Array.from(iterVal) : Object.keys(iterVal || {}));
          for (const item of items) {
            this.checkSteps();
            const loopEnv = new Environment(env);
            loopEnv.define(stmt.iterator, item);
            try {
              this.executeBlock(stmt.body, loopEnv);
            } catch (e) {
              if (e === 'break') break;
              if (e === 'continue') continue;
              throw e;
            }
          }
          return null;
        }
        case 'RepeatStatement': {
          const times = Math.floor(Number(this.evalExpr(stmt.count, env)) || 0);
          for (let i = 0; i < times; i++) {
            this.checkSteps();
            try {
              this.executeBlock(stmt.body, new Environment(env));
            } catch (e) {
              if (e === 'break') break;
              if (e === 'continue') continue;
              throw e;
            }
          }
          return null;
        }
        case 'BreakStatement': throw 'break';
        case 'ContinueStatement': throw 'continue';
        case 'TryStatement': {
          try {
            this.executeBlock(stmt.tryBlock, new Environment(env));
          } catch (e) {
            if (stmt.catchBlock) {
              const catchEnv = new Environment(env);
              if (stmt.catchVar) {
                catchEnv.define(stmt.catchVar, e.message || String(e));
              }
              this.executeBlock(stmt.catchBlock, catchEnv);
            }
          } finally {
            if (stmt.finallyBlock) {
              this.executeBlock(stmt.finallyBlock, new Environment(env));
            }
          }
          return null;
        }
        case 'FunctionDeclaration': {
          const fnClosure = (...args) => {
            const callEnv = new Environment(env);
            stmt.params.forEach((param, i) => {
              let val = args[i];
              if (val === undefined && stmt.defaults && stmt.defaults[param]) {
                val = this.evalExpr(stmt.defaults[param], env);
              }
              callEnv.define(param, val !== undefined ? val : null);
            });
            try {
              this.executeBlock(stmt.body, callEnv);
              return null;
            } catch (e) {
              if (e instanceof ReturnException) return e.value;
              throw e;
            }
          };
          env.define(stmt.name, fnClosure);
          return null;
        }
        case 'StructDeclaration': {
          const constructor = (...args) => {
            const inst = { __struct: stmt.name };
            stmt.fields.forEach((f, i) => {
              inst[f] = args[i] !== undefined ? args[i] : null;
            });
            // Attach methods
            stmt.methods.forEach((m) => {
              inst[m.name] = (...mArgs) => {
                const methodEnv = new Environment(env);
                methodEnv.define('self', inst);
                m.params.forEach((p, pi) => {
                  if (p !== 'self') {
                    methodEnv.define(p, mArgs[pi - 1] !== undefined ? mArgs[pi - 1] : null);
                  }
                });
                try {
                  this.executeBlock(m.body, methodEnv);
                  return null;
                } catch (e) {
                  if (e instanceof ReturnException) return e.value;
                  throw e;
                }
              };
            });
            return inst;
          };
          env.define(stmt.name, constructor);
          return null;
        }
        case 'EnumDeclaration': {
          const enumObj = { __enumType: stmt.name };
          stmt.variants.forEach((v) => {
            enumObj[v.name] = (...args) => ({
              __enum: stmt.name,
              __variant: v.name,
              __args: args
            });
          });
          env.define(stmt.name, enumObj);
          return null;
        }
        case 'MatchStatement': {
          const targetVal = this.evalExpr(stmt.target, env);
          for (const c of stmt.cases) {
            const matchEnv = new Environment(env);
            if (this.matchPattern(c.pattern, targetVal, matchEnv)) {
              this.executeBlock(c.body, matchEnv);
              break;
            }
          }
          return null;
        }
        case 'ExpressionStatement': {
          this.evalExpr(stmt.expression, env);
          return null;
        }
        default:
          return null;
      }
    }

    matchPattern(pattern, val, env) {
      if (!pattern) return false;
      if (pattern.type === 'IdentifierPattern') {
        if (pattern.name === '_') return true;
        env.define(pattern.name, val);
        return true;
      }
      if (pattern.type === 'VariantPattern' || pattern.type === 'EnumPattern') {
        if (val && val.__variant === pattern.variant) {
          if (pattern.params && val.__args) {
            pattern.params.forEach((p, idx) => env.define(p, val.__args[idx]));
          }
          return true;
        }
        return false;
      }
      if (pattern.type === 'LiteralPattern') {
        const lit = this.evalExpr(pattern.value, env);
        return lit === val;
      }
      return false;
    }

    executeBlock(block, env) {
      if (!block || !block.statements) return;
      for (const s of block.statements) {
        this.executeStatement(s, env);
      }
    }

    evalExpr(expr, env) {
      this.checkSteps();
      if (!expr) return null;

      switch (expr.type) {
        case 'Literal':
          return expr.value;
        case 'Identifier': {
          const val = env.get(expr.name);
          if (val === undefined) {
            throw new UnfishRuntimeError(`Undefined variable '${expr.name}'`, 'NameError');
          }
          return val;
        }
        case 'InterpolatedString': {
          const raw = expr.raw;
          return raw.replace(/\{([^}]+)\}/g, (_, inner) => {
            try {
              const lexer = new Lexer(inner);
              const { tokens } = lexer.tokenize();
              const parser = new Parser(tokens);
              const ast = parser.expression();
              const v = this.evalExpr(ast, env);
              return this.stringify(v);
            } catch (e) {
              return `{${inner}}`;
            }
          });
        }
        case 'ArrayLiteral': {
          const res = [];
          for (const el of expr.elements) {
            if (el.type === 'Spread') {
              const spreadVal = this.evalExpr(el.expr, env);
              if (Array.isArray(spreadVal)) res.push(...spreadVal);
              else res.push(spreadVal);
            } else {
              res.push(this.evalExpr(el, env));
            }
          }
          return res;
        }
        case 'MapLiteral': {
          const map = {};
          for (const ent of expr.entries) {
            map[ent.key] = this.evalExpr(ent.val, env);
          }
          return map;
        }
        case 'BinaryExpression': {
          const left = this.evalExpr(expr.left, env);
          const right = this.evalExpr(expr.right, env);
          switch (expr.operator) {
            case '+':
              if (typeof left === 'string' || typeof right === 'string') {
                return this.stringify(left) + this.stringify(right);
              }
              return (Number(left) || 0) + (Number(right) || 0);
            case '-': return (Number(left) || 0) - (Number(right) || 0);
            case '*': return (Number(left) || 0) * (Number(right) || 0);
            case '/':
              if (Number(right) === 0) throw new UnfishRuntimeError('Division by zero', 'DivisionByZero');
              return (Number(left) || 0) / Number(right);
            case '%': return (Number(left) || 0) % (Number(right) || 1);
            case '^': return Math.pow(Number(left) || 0, Number(right) || 0);
            case '==': return left === right;
            case '!=': return left !== right;
            case '<': return left < right;
            case '<=': return left <= right;
            case '>': return left > right;
            case '>=': return left >= right;
            case 'and': return this.isTruthy(left) && this.isTruthy(right);
            case 'or': return this.isTruthy(left) || this.isTruthy(right);
            default: return null;
          }
        }
        case 'UnaryExpression': {
          const val = this.evalExpr(expr.right, env);
          if (expr.operator === '-') return -(Number(val) || 0);
          if (expr.operator === 'not' || expr.operator === '!') return !this.isTruthy(val);
          return val;
        }
        case 'CallExpression': {
          const callee = this.evalExpr(expr.callee, env);
          if (typeof callee !== 'function') {
            throw new UnfishRuntimeError(`Attempted to call non-function of type '${typeof callee}'`, 'TypeError');
          }
          const args = [];
          for (const a of expr.args) {
            if (a.type === 'Spread') {
              const spread = this.evalExpr(a.expr, env);
              if (Array.isArray(spread)) args.push(...spread);
              else args.push(spread);
            } else {
              args.push(this.evalExpr(a, env));
            }
          }
          return callee(...args);
        }
        case 'MemberExpression': {
          const obj = this.evalExpr(expr.object, env);
          if (obj === null || obj === undefined) {
            throw new UnfishRuntimeError(`Cannot read property '${expr.property}' of null/undefined`, 'NullReference');
          }
          if (typeof obj === 'object') {
            return obj[expr.property];
          }
          return undefined;
        }
        case 'IndexExpression': {
          const obj = this.evalExpr(expr.object, env);
          const idx = this.evalExpr(expr.index, env);
          if (Array.isArray(obj)) {
            const realIdx = typeof idx === 'number' && idx < 0 ? obj.length + idx : idx;
            return obj[realIdx] !== undefined ? obj[realIdx] : null;
          }
          if (typeof obj === 'string') {
            const realIdx = typeof idx === 'number' && idx < 0 ? obj.length + idx : idx;
            return (realIdx >= 0 && realIdx < obj.length) ? obj[realIdx] : '';
          }
          if (obj && typeof obj === 'object') {
            return obj[idx] !== undefined ? obj[idx] : null;
          }
          return null;
        }
        case 'Lambda': {
          return (...args) => {
            const lambdaEnv = new Environment(env);
            expr.params.forEach((p, i) => lambdaEnv.define(p, args[i] !== undefined ? args[i] : null));
            if (expr.bodyExpr) return this.evalExpr(expr.bodyExpr, lambdaEnv);
            try {
              this.executeBlock(expr.body, lambdaEnv);
              return null;
            } catch (e) {
              if (e instanceof ReturnException) return e.value;
              throw e;
            }
          };
        }
        case 'PipeExpression': {
          const leftVal = this.evalExpr(expr.left, env);
          if (expr.right.type === 'CallExpression') {
            const callee = this.evalExpr(expr.right.callee, env);
            const args = [leftVal, ...expr.right.args.map((a) => this.evalExpr(a, env))];
            return callee(...args);
          } else if (expr.right.type === 'Identifier') {
            const fn = this.evalExpr(expr.right, env);
            return fn(leftVal);
          }
          return leftVal;
        }
        default:
          return null;
      }
    }

    isTruthy(val) {
      if (val === null || val === undefined || val === false || val === 0 || val === '') return false;
      return true;
    }
  }

  // =========================================================================
  // 5. UNIFIED ENGINE FACADE
  // =========================================================================

  class UnfishEngine {
    constructor() {
      this.interpreter = new Interpreter();
    }

    tokenize(code) {
      const lexer = new Lexer(code);
      return lexer.tokenize();
    }

    parse(code) {
      const { tokens, errors: lexErrors } = this.tokenize(code);
      const parser = new Parser(tokens);
      const ast = parser.parse();
      if (lexErrors.length > 0) {
        ast.errors = [...lexErrors, ...(ast.errors || [])];
      }
      return ast;
    }

    disassemble(code) {
      const ast = this.parse(code);
      return Disassembler.disassemble(ast);
    }

    formatAst(ast) {
      if (!ast) return '(empty)';
      const formatNode = (n, indent = 0) => {
        const ind = '  '.repeat(indent);
        if (!n) return `${ind}nil`;
        if (typeof n !== 'object') return `${ind}${JSON.stringify(n)}`;
        if (Array.isArray(n)) {
          return n.map((item) => formatNode(item, indent)).join('\n');
        }
        const fields = Object.keys(n)
          .filter((k) => k !== 'type')
          .map((k) => `${k}: ${typeof n[k] === 'object' ? '\n' + formatNode(n[k], indent + 1) : JSON.stringify(n[k])}`)
          .join(', ');
        return `${ind}(${n.type} ${fields})`;
      };
      return formatNode(ast);
    }

    run(code, outputCallback = null) {
      const interp = new Interpreter(outputCallback);
      const ast = this.parse(code);
      if (ast.errors && ast.errors.length > 0) {
        return {
          exit_code: 1,
          stdout: '',
          stderr: ast.errors.join('\n'),
          ast
        };
      }
      const res = interp.execute(ast);
      res.ast = ast;
      return res;
    }

    // Keep backwards compatible methods
    parseBlocks(code) {
      const lines = code.split('\n');
      const statements = [];

      for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        const trimmed = line.trim();
        if (!trimmed || trimmed.startsWith('#')) continue;

        const indent = line.search(/\S/);

        if (trimmed.startsWith('let ')) {
          const rest = trimmed.slice(4).trim();
          const eqIdx = rest.indexOf('=');
          if (eqIdx !== -1) {
            statements.push({
              kind: 'let',
              name: rest.slice(0, eqIdx).trim(),
              value: rest.slice(eqIdx + 1).trim(),
              indent
            });
          } else {
            statements.push({ kind: 'let', name: rest, value: '', indent });
          }
        } else if (trimmed.startsWith('say ')) {
          statements.push({
            kind: 'say',
            value: trimmed.slice(4).trim(),
            indent
          });
        } else if (trimmed.startsWith('function ') || trimmed.startsWith('fn ')) {
          const header = trimmed.replace(/^(function|fn)\s+/, '').replace(/:$/, '').trim();
          const parenIdx = header.indexOf('(');
          const name = parenIdx !== -1 ? header.slice(0, parenIdx).trim() : header;
          const closeParen = header.indexOf(')');
          const params = (parenIdx !== -1 && closeParen !== -1) ? header.slice(parenIdx + 1, closeParen).trim() : '';
          statements.push({
            kind: 'function',
            name,
            params,
            indent
          });
        } else if (trimmed.startsWith('if ')) {
          statements.push({
            kind: 'if',
            condition: trimmed.slice(3).replace(/:$/, '').trim(),
            indent
          });
        } else if (trimmed.startsWith('while ')) {
          statements.push({
            kind: 'while',
            condition: trimmed.slice(6).replace(/:$/, '').trim(),
            indent
          });
        } else if (trimmed.startsWith('for ')) {
          statements.push({
            kind: 'for',
            iterator: trimmed.slice(4).replace(/:$/, '').trim(),
            indent
          });
        } else if (/^repeat\s+.+\s+times\s*:?$/.test(trimmed)) {
          const count = trimmed.replace(/^repeat\s+/, '').replace(/\s+times\s*:?$/, '').trim();
          statements.push({
            kind: 'repeat',
            count,
            indent
          });
        } else if (trimmed.startsWith('struct ')) {
          statements.push({
            kind: 'struct',
            name: trimmed.slice(7).replace(/:$/, '').trim(),
            indent
          });
        } else if (trimmed.startsWith('enum ')) {
          statements.push({
            kind: 'enum',
            name: trimmed.slice(5).replace(/:$/, '').trim(),
            indent
          });
        } else if (trimmed.startsWith('match ')) {
          statements.push({
            kind: 'match',
            target: trimmed.slice(6).replace(/:$/, '').trim(),
            indent
          });
        } else if (trimmed.startsWith('when ')) {
          statements.push({
            kind: 'when',
            pattern: trimmed.slice(5).replace(/:$/, '').trim(),
            indent
          });
        } else if (trimmed === 'try' || trimmed === 'try:') {
          statements.push({ kind: 'try', indent });
        } else if (trimmed.startsWith('catch')) {
          const rest = trimmed.slice(5).replace(/:$/, '').trim();
          statements.push({ kind: 'catch', variable: rest, indent });
        } else if (trimmed === 'finally' || trimmed === 'finally:') {
          statements.push({ kind: 'finally', indent });
        } else if (trimmed.startsWith('spawn ')) {
          statements.push({ kind: 'spawn', expr: trimmed.slice(6).trim(), indent });
        } else if (trimmed === 'yield') {
          statements.push({ kind: 'yield', indent });
        } else if (trimmed.startsWith('return ') || trimmed === 'return') {
          statements.push({
            kind: 'return',
            value: trimmed.length > 6 ? trimmed.slice(7).trim() : '',
            indent
          });
        } else {
          const eqIdx = trimmed.indexOf('=');
          if (eqIdx !== -1 && /^[a-zA-Z_][a-zA-Z0-9_.]*\s*=/.test(trimmed)) {
            statements.push({
              kind: 'set',
              name: trimmed.slice(0, eqIdx).trim(),
              value: trimmed.slice(eqIdx + 1).trim(),
              indent
            });
          } else {
            statements.push({
              kind: 'expr',
              expression: trimmed,
              indent
            });
          }
        }
      }

      return {
        schema: 'unfish_blocks_v1',
        statements
      };
    }

    blocksToCode(blocksData) {
      if (!blocksData || !blocksData.statements || blocksData.statements.length === 0) {
        return '';
      }

      const lines = [];
      for (const stmt of blocksData.statements) {
        const ind = ' '.repeat(Math.max(0, stmt.indent || 0));
        switch (stmt.kind) {
          case 'let':
            lines.push(`${ind}let ${stmt.name || 'x'} = ${stmt.value !== undefined ? stmt.value : '0'}`);
            break;
          case 'set':
            lines.push(`${ind}${stmt.name || 'x'} = ${stmt.value !== undefined ? stmt.value : '0'}`);
            break;
          case 'say':
            lines.push(`${ind}say ${stmt.value !== undefined ? stmt.value : '""'}`);
            break;
          case 'function':
            lines.push(`${ind}function ${stmt.name || 'fn_name'}(${stmt.params || ''}):`);
            break;
          case 'return':
            lines.push(stmt.value ? `${ind}return ${stmt.value}` : `${ind}return`);
            break;
          case 'if':
            lines.push(`${ind}if ${stmt.condition || 'true'}:`);
            break;
          case 'while':
            lines.push(`${ind}while ${stmt.condition || 'true'}:`);
            break;
          case 'for':
            lines.push(`${ind}for ${stmt.iterator || 'item in items'}:`);
            break;
          case 'repeat':
            lines.push(`${ind}repeat ${stmt.count || '5'} times:`);
            break;
          case 'struct':
            lines.push(`${ind}struct ${stmt.name || 'Record'}:`);
            break;
          case 'enum':
            lines.push(`${ind}enum ${stmt.name || 'Status'}:`);
            break;
          case 'match':
            lines.push(`${ind}match ${stmt.target || 'val'}:`);
            break;
          case 'when':
            lines.push(`${ind}when ${stmt.pattern || '_'}:`);
            break;
          case 'try':
            lines.push(`${ind}try:`);
            break;
          case 'catch':
            lines.push(stmt.variable ? `${ind}catch ${stmt.variable}:` : `${ind}catch:`);
            break;
          case 'finally':
            lines.push(`${ind}finally:`);
            break;
          case 'spawn':
            lines.push(`${ind}spawn ${stmt.expr || 'fn(): null'}`);
            break;
          case 'yield':
            lines.push(`${ind}yield`);
            break;
          case 'expr':
          default:
            lines.push(`${ind}${stmt.expression || stmt.value || ''}`);
            break;
        }
      }

      return lines.join('\n') + (lines.length > 0 ? '\n' : '');
    }

    renderEditableBlocks(blocksData, container, onChangeCallback) {
      container.innerHTML = '';

      if (!blocksData.statements || blocksData.statements.length === 0) {
        container.innerHTML = `
          <div class="blocks-empty-state">
            <span class="empty-icon">🧩</span>
            <p>No blocks on the canvas yet.</p>
            <span class="empty-hint">Click any block in the <strong>Toolbox Palette</strong> to add it to your program!</span>
          </div>
        `;
        return;
      }

      const stmts = blocksData.statements;

      stmts.forEach((stmt, index) => {
        const blockEl = document.createElement('div');
        blockEl.className = `editable-block block-kind-${stmt.kind}`;
        blockEl.dataset.index = index;

        const indentSpaces = stmt.indent || 0;
        if (indentSpaces > 0) {
          blockEl.style.marginLeft = `${(indentSpaces / 4) * 1.8}rem`;
          blockEl.classList.add('nested-block');
        }

        const headerEl = document.createElement('div');
        headerEl.className = 'block-header';

        const tagEl = document.createElement('span');
        tagEl.className = `block-badge badge-${stmt.kind}`;
        tagEl.textContent = stmt.kind.toUpperCase();
        headerEl.appendChild(tagEl);

        const titleEl = document.createElement('span');
        titleEl.className = 'block-title-label';
        titleEl.textContent = this.getBlockTitle(stmt.kind);
        headerEl.appendChild(titleEl);

        const controlsEl = document.createElement('div');
        controlsEl.className = 'block-controls';

        // Move Up
        const btnUp = document.createElement('button');
        btnUp.className = 'block-btn';
        btnUp.title = 'Move up';
        btnUp.innerHTML = '▲';
        btnUp.disabled = (index === 0);
        btnUp.onclick = (e) => {
          e.stopPropagation();
          const tmp = stmts[index];
          stmts[index] = stmts[index - 1];
          stmts[index - 1] = tmp;
          this.renderEditableBlocks(blocksData, container, onChangeCallback);
          if (onChangeCallback) onChangeCallback(blocksData);
        };
        controlsEl.appendChild(btnUp);

        // Move Down
        const btnDown = document.createElement('button');
        btnDown.className = 'block-btn';
        btnDown.title = 'Move down';
        btnDown.innerHTML = '▼';
        btnDown.disabled = (index === stmts.length - 1);
        btnDown.onclick = (e) => {
          e.stopPropagation();
          const tmp = stmts[index];
          stmts[index] = stmts[index + 1];
          stmts[index + 1] = tmp;
          this.renderEditableBlocks(blocksData, container, onChangeCallback);
          if (onChangeCallback) onChangeCallback(blocksData);
        };
        controlsEl.appendChild(btnDown);

        // Dedent
        const btnDedent = document.createElement('button');
        btnDedent.className = 'block-btn';
        btnDedent.title = 'Dedent block';
        btnDedent.innerHTML = '⇤';
        btnDedent.disabled = (indentSpaces <= 0);
        btnDedent.onclick = (e) => {
          e.stopPropagation();
          stmt.indent = Math.max(0, (stmt.indent || 0) - 4);
          this.renderEditableBlocks(blocksData, container, onChangeCallback);
          if (onChangeCallback) onChangeCallback(blocksData);
        };
        controlsEl.appendChild(btnDedent);

        // Indent
        const btnIndent = document.createElement('button');
        btnIndent.className = 'block-btn';
        btnIndent.title = 'Indent block';
        btnIndent.innerHTML = '⇥';
        btnIndent.onclick = (e) => {
          e.stopPropagation();
          stmt.indent = (stmt.indent || 0) + 4;
          this.renderEditableBlocks(blocksData, container, onChangeCallback);
          if (onChangeCallback) onChangeCallback(blocksData);
        };
        controlsEl.appendChild(btnIndent);

        // Delete
        const btnDel = document.createElement('button');
        btnDel.className = 'block-btn block-btn-delete';
        btnDel.title = 'Delete block';
        btnDel.innerHTML = '✕';
        btnDel.onclick = (e) => {
          e.stopPropagation();
          stmts.splice(index, 1);
          this.renderEditableBlocks(blocksData, container, onChangeCallback);
          if (onChangeCallback) onChangeCallback(blocksData);
        };
        controlsEl.appendChild(btnDel);

        headerEl.appendChild(controlsEl);
        blockEl.appendChild(headerEl);

        const bodyEl = document.createElement('div');
        bodyEl.className = 'block-body';

        this.buildBlockInputs(stmt, bodyEl, () => {
          if (onChangeCallback) onChangeCallback(blocksData);
        });

        blockEl.appendChild(bodyEl);
        container.appendChild(blockEl);
      });
    }

    getBlockTitle(kind) {
      switch (kind) {
        case 'say': return 'Print / Say';
        case 'let': return 'Define Variable';
        case 'set': return 'Reassign Variable';
        case 'function': return 'Function Declaration';
        case 'return': return 'Return Statement';
        case 'if': return 'If Condition';
        case 'while': return 'While Loop';
        case 'for': return 'For-In Loop';
        case 'repeat': return 'Repeat Times';
        case 'struct': return 'Struct Definition';
        case 'enum': return 'Enum Definition';
        case 'match': return 'Pattern Match';
        case 'when': return 'Match Case';
        case 'try': return 'Try Recovery';
        case 'catch': return 'Catch Error';
        case 'finally': return 'Finally Block';
        case 'spawn': return 'Spawn Fiber';
        case 'yield': return 'Yield Scheduler';
        default: return 'Expression';
      }
    }

    buildBlockInputs(stmt, parentEl, onFieldUpdate) {
      const makeInput = (val, placeholder, onUpdate) => {
        const input = document.createElement('input');
        input.type = 'text';
        input.className = 'block-field-input';
        input.value = val !== undefined ? val : '';
        input.placeholder = placeholder;
        input.spellcheck = false;
        input.addEventListener('input', (e) => {
          onUpdate(e.target.value);
          onFieldUpdate();
        });
        return input;
      };

      const makeLabel = (text) => {
        const label = document.createElement('span');
        label.className = 'block-syntax-label';
        label.textContent = text;
        return label;
      };

      switch (stmt.kind) {
        case 'let':
          parentEl.appendChild(makeLabel('let '));
          parentEl.appendChild(makeInput(stmt.name, 'var_name', (v) => stmt.name = v));
          parentEl.appendChild(makeLabel(' = '));
          parentEl.appendChild(makeInput(stmt.value, 'initial_value', (v) => stmt.value = v));
          break;
        case 'set':
          parentEl.appendChild(makeInput(stmt.name, 'target_var', (v) => stmt.name = v));
          parentEl.appendChild(makeLabel(' = '));
          parentEl.appendChild(makeInput(stmt.value, 'expression', (v) => stmt.value = v));
          break;
        case 'say':
          parentEl.appendChild(makeLabel('say '));
          parentEl.appendChild(makeInput(stmt.value, 'expression to print', (v) => stmt.value = v));
          break;
        case 'function':
          parentEl.appendChild(makeLabel('function '));
          parentEl.appendChild(makeInput(stmt.name, 'function_name', (v) => stmt.name = v));
          parentEl.appendChild(makeLabel(' ( '));
          parentEl.appendChild(makeInput(stmt.params, 'param1, param2', (v) => stmt.params = v));
          parentEl.appendChild(makeLabel(' ) :'));
          break;
        case 'return':
          parentEl.appendChild(makeLabel('return '));
          parentEl.appendChild(makeInput(stmt.value, 'value or expression', (v) => stmt.value = v));
          break;
        case 'if':
          parentEl.appendChild(makeLabel('if '));
          parentEl.appendChild(makeInput(stmt.condition, 'boolean condition', (v) => stmt.condition = v));
          parentEl.appendChild(makeLabel(' :'));
          break;
        case 'while':
          parentEl.appendChild(makeLabel('while '));
          parentEl.appendChild(makeInput(stmt.condition, 'loop condition', (v) => stmt.condition = v));
          parentEl.appendChild(makeLabel(' :'));
          break;
        case 'for':
          parentEl.appendChild(makeLabel('for '));
          parentEl.appendChild(makeInput(stmt.iterator, 'item in collection', (v) => stmt.iterator = v));
          parentEl.appendChild(makeLabel(' :'));
          break;
        case 'repeat':
          parentEl.appendChild(makeLabel('repeat '));
          parentEl.appendChild(makeInput(stmt.count, 'number of times', (v) => stmt.count = v));
          parentEl.appendChild(makeLabel(' times:'));
          break;
        case 'struct':
          parentEl.appendChild(makeLabel('struct '));
          parentEl.appendChild(makeInput(stmt.name, 'TypeName', (v) => stmt.name = v));
          parentEl.appendChild(makeLabel(' :'));
          break;
        case 'enum':
          parentEl.appendChild(makeLabel('enum '));
          parentEl.appendChild(makeInput(stmt.name, 'EnumName', (v) => stmt.name = v));
          parentEl.appendChild(makeLabel(' :'));
          break;
        case 'match':
          parentEl.appendChild(makeLabel('match '));
          parentEl.appendChild(makeInput(stmt.target, 'target_expression', (v) => stmt.target = v));
          parentEl.appendChild(makeLabel(' :'));
          break;
        case 'when':
          parentEl.appendChild(makeLabel('when '));
          parentEl.appendChild(makeInput(stmt.pattern, 'Pattern(arg)', (v) => stmt.pattern = v));
          parentEl.appendChild(makeLabel(' :'));
          break;
        case 'try':
          parentEl.appendChild(makeLabel('try:'));
          break;
        case 'catch':
          parentEl.appendChild(makeLabel('catch '));
          parentEl.appendChild(makeInput(stmt.variable, 'err (optional)', (v) => stmt.variable = v));
          parentEl.appendChild(makeLabel(' :'));
          break;
        case 'finally':
          parentEl.appendChild(makeLabel('finally:'));
          break;
        case 'spawn':
          parentEl.appendChild(makeLabel('spawn '));
          parentEl.appendChild(makeInput(stmt.expr, 'function_call()', (v) => stmt.expr = v));
          break;
        case 'yield':
          parentEl.appendChild(makeLabel('yield'));
          break;
        case 'expr':
        default:
          parentEl.appendChild(makeInput(stmt.expression || stmt.value, 'statement or expression', (v) => {
            stmt.expression = v;
            stmt.value = v;
          }));
          break;
      }
    }
  }

  global.UnfishEngine = UnfishEngine;
  global.UnfishLexer = Lexer;
  global.UnfishParser = Parser;
  global.UnfishInterpreter = Interpreter;
  global.UnfishDisassembler = Disassembler;

})(typeof window !== 'undefined' ? window : global);
