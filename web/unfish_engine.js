/**
 * 🐡 Unfish Browser Engine & Blocks System (v2.0)
 * Zero-dependency pure JavaScript runtime, two-way visual block generator, and block editor
 */

class UnfishEngine {
  constructor() {
    this.output = [];
    this.errors = [];
  }

  /**
   * Parse Unfish code into visual block AST (schema: unfish_blocks_v1)
   */
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
            indent: indent
          });
        } else {
          statements.push({ kind: 'let', name: rest, value: '', indent: indent });
        }
      } else if (trimmed.startsWith('say ')) {
        statements.push({
          kind: 'say',
          value: trimmed.slice(4).trim(),
          indent: indent
        });
      } else if (trimmed.startsWith('function ') || trimmed.startsWith('fn ')) {
        const header = trimmed.replace(/^(function|fn)\s+/, '').replace(/:$/, '').trim();
        const parenIdx = header.indexOf('(');
        const name = parenIdx !== -1 ? header.slice(0, parenIdx).trim() : header;
        const closeParen = header.indexOf(')');
        const params = (parenIdx !== -1 && closeParen !== -1) ? header.slice(parenIdx + 1, closeParen).trim() : '';
        statements.push({
          kind: 'function',
          name: name,
          params: params,
          indent: indent
        });
      } else if (trimmed.startsWith('if ')) {
        statements.push({
          kind: 'if',
          condition: trimmed.slice(3).replace(/:$/, '').trim(),
          indent: indent
        });
      } else if (trimmed.startsWith('while ')) {
        statements.push({
          kind: 'while',
          condition: trimmed.slice(6).replace(/:$/, '').trim(),
          indent: indent
        });
      } else if (trimmed.startsWith('for ')) {
        statements.push({
          kind: 'for',
          iterator: trimmed.slice(4).replace(/:$/, '').trim(),
          indent: indent
        });
      } else if (/^repeat\s+.+\s+times\s*:?$/.test(trimmed)) {
        const count = trimmed.replace(/^repeat\s+/, '').replace(/\s+times\s*:?$/, '').trim();
        statements.push({
          kind: 'repeat',
          count: count,
          indent: indent
        });
      } else if (trimmed.startsWith('struct ')) {
        statements.push({
          kind: 'struct',
          name: trimmed.slice(7).replace(/:$/, '').trim(),
          indent: indent
        });
      } else if (trimmed.startsWith('enum ')) {
        statements.push({
          kind: 'enum',
          name: trimmed.slice(5).replace(/:$/, '').trim(),
          indent: indent
        });
      } else if (trimmed.startsWith('match ')) {
        statements.push({
          kind: 'match',
          target: trimmed.slice(6).replace(/:$/, '').trim(),
          indent: indent
        });
      } else if (trimmed.startsWith('when ')) {
        statements.push({
          kind: 'when',
          pattern: trimmed.slice(5).replace(/:$/, '').trim(),
          indent: indent
        });
      } else if (trimmed === 'try' || trimmed === 'try:') {
        statements.push({ kind: 'try', indent: indent });
      } else if (trimmed.startsWith('catch')) {
        const rest = trimmed.slice(5).replace(/:$/, '').trim();
        statements.push({ kind: 'catch', variable: rest, indent: indent });
      } else if (trimmed === 'finally' || trimmed === 'finally:') {
        statements.push({ kind: 'finally', indent: indent });
      } else if (trimmed.startsWith('spawn ')) {
        statements.push({ kind: 'spawn', expr: trimmed.slice(6).trim(), indent: indent });
      } else if (trimmed === 'yield') {
        statements.push({ kind: 'yield', indent: indent });
      } else if (trimmed.startsWith('return ') || trimmed === 'return') {
        statements.push({
          kind: 'return',
          value: trimmed.length > 6 ? trimmed.slice(7).trim() : '',
          indent: indent
        });
      } else {
        const eqIdx = trimmed.indexOf('=');
        if (eqIdx !== -1 && /^[a-zA-Z_][a-zA-Z0-9_.]*\s*=/.test(trimmed)) {
          statements.push({
            kind: 'set',
            name: trimmed.slice(0, eqIdx).trim(),
            value: trimmed.slice(eqIdx + 1).trim(),
            indent: indent
          });
        } else {
          statements.push({
            kind: 'expr',
            expression: trimmed,
            indent: indent
          });
        }
      }
    }

    return {
      schema: 'unfish_blocks_v1',
      statements: statements
    };
  }

  /**
   * Convert block AST (schema: unfish_blocks_v1) to clean, indented Unfish source code
   */
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

  /**
   * Render interactive editable blocks into a DOM container
   */
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

      // 1. Block Header / Grip
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

      // Controls (Move Up, Move Down, Indent, Dedent, Delete)
      const controlsEl = document.createElement('div');
      controlsEl.className = 'block-controls';

      // Move Up
      const btnUp = document.createElement('button');
      btnUp.className = 'block-btn';
      btnUp.title = 'Move block up';
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
      btnDown.title = 'Move block down';
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

      // Dedent (<-)
      const btnDedent = document.createElement('button');
      btnDedent.className = 'block-btn';
      btnDedent.title = 'Dedent block (decrease indentation)';
      btnDedent.innerHTML = '⇤';
      btnDedent.disabled = (indentSpaces <= 0);
      btnDedent.onclick = (e) => {
        e.stopPropagation();
        stmt.indent = Math.max(0, (stmt.indent || 0) - 4);
        this.renderEditableBlocks(blocksData, container, onChangeCallback);
        if (onChangeCallback) onChangeCallback(blocksData);
      };
      controlsEl.appendChild(btnDedent);

      // Indent (->)
      const btnIndent = document.createElement('button');
      btnIndent.className = 'block-btn';
      btnIndent.title = 'Indent block (nest inside parent block)';
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

      // 2. Block Body with In-Place Editable Input Fields
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
        parentEl.appendChild(makeInput(stmt.value, 'value or expression (optional)', (v) => stmt.value = v));
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

  escape(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  /**
   * Lightweight client-side simulation when backend is unreachable
   */
  simulate(code) {
    this.output = [];
    this.errors = [];
    const lines = code.split('\n');
    const scope = {};

    for (let i = 0; i < lines.length; i++) {
      const line = lines[i].trim();
      if (!line || line.startsWith('#')) continue;

      try {
        if (line.startsWith('say ')) {
          let expr = line.slice(4).trim();
          let val = this.evalExpr(expr, scope);
          this.output.push(String(val));
        } else if (line.startsWith('let ')) {
          let rest = line.slice(4).trim();
          let eq = rest.indexOf('=');
          if (eq !== -1) {
            let name = rest.slice(0, eq).trim();
            let valExpr = rest.slice(eq + 1).trim();
            scope[name] = this.evalExpr(valExpr, scope);
          }
        }
      } catch (err) {
        this.errors.push(`Line ${i + 1}: ${err.message}`);
      }
    }

    return {
      exit_code: this.errors.length > 0 ? 1 : 0,
      stdout: this.output.join('\n') + (this.output.length > 0 ? '\n' : ''),
      stderr: this.errors.join('\n')
    };
  }

  evalExpr(expr, scope) {
    if (expr.startsWith('f"') && expr.endsWith('"')) {
      let content = expr.slice(2, -1);
      return content.replace(/\{([^}]+)\}/g, (_, inner) => {
        return this.evalExpr(inner.trim(), scope);
      });
    }
    if (expr.startsWith('"') && expr.endsWith('"')) {
      return expr.slice(1, -1);
    }
    if (!isNaN(Number(expr))) {
      return Number(expr);
    }
    if (scope[expr] !== undefined) {
      return scope[expr];
    }
    return expr;
  }
}

window.UnfishEngine = UnfishEngine;
