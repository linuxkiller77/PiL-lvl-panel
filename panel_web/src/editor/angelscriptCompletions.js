// AngelScript + PiLab completion definitions for the Script Console.
// This file is intentionally data-only so the editor page can stay focused on
// PLC upload/status behavior.

const keyword = (label, detail = 'AngelScript keyword', boost = 1) => ({ label, detail, icon: 'keyword', boost });
const type = (label, detail = 'AngelScript type', boost = 2) => ({ label, detail, icon: 'class', boost });
const literal = (label, detail = 'AngelScript literal', boost = 2) => ({ label, detail, icon: 'constant', boost });
const fn = (label, detail = 'AngelScript function', insert = undefined, boost = 2) => ({ label, detail, icon: 'function', insert, boost });
const variable = (label, detail = 'variable', boost = 1) => ({ label, detail, icon: 'variable', boost });
const snippet = (label, detail, insert, tabStops = undefined, boost = 9) => ({ label, detail, icon: 'snippet', insert, tabStops, boost });

export const ANGELSCRIPT_KEYWORD_COMPLETIONS = [
  // Flow/control
  keyword('if', 'conditional statement', 6),
  keyword('else', 'else branch', 5),
  keyword('for', 'for loop', 6),
  keyword('while', 'while loop', 6),
  keyword('do', 'do/while loop', 3),
  keyword('switch', 'switch statement', 4),
  keyword('case', 'switch case', 4),
  keyword('default', 'switch default', 3),
  keyword('break', 'exit loop or switch', 4),
  keyword('continue', 'continue loop', 4),
  keyword('return', 'return from function', 5),

  // Declarations / object model
  keyword('class', 'class declaration', 4),
  keyword('interface', 'interface declaration', 3),
  keyword('enum', 'enum declaration', 3),
  keyword('funcdef', 'function definition type', 2),
  keyword('namespace', 'namespace declaration', 2),
  keyword('mixin', 'mixin class declaration', 1),
  keyword('shared', 'shared entity declaration', 2),
  keyword('external', 'external entity declaration', 1),
  keyword('final', 'prevent override/inheritance', 1),
  keyword('override', 'override virtual method', 1),
  keyword('abstract', 'abstract class/method', 1),

  // Qualifiers / handles / casting
  keyword('const', 'constant qualifier', 4),
  keyword('private', 'private access', 2),
  keyword('protected', 'protected access', 2),
  keyword('in', 'input parameter qualifier', 3),
  keyword('out', 'output parameter qualifier', 3),
  keyword('inout', 'input/output parameter qualifier', 3),
  keyword('is', 'object handle identity comparison', 2),
  keyword('cast', 'cast expression', 2),
  keyword('auto', 'automatic type deduction', 2),
];

export const ANGELSCRIPT_TYPE_COMPLETIONS = [
  type('void', 'no return value', 8),
  type('bool', 'boolean type', 8),
  type('int', 'signed integer', 8),
  type('uint', 'unsigned integer', 8),
  type('int8', '8-bit signed integer', 4),
  type('uint8', '8-bit unsigned integer', 4),
  type('int16', '16-bit signed integer', 4),
  type('uint16', '16-bit unsigned integer', 4),
  type('int32', '32-bit signed integer', 4),
  type('uint32', '32-bit unsigned integer', 4),
  type('int64', '64-bit signed integer', 3),
  type('uint64', '64-bit unsigned integer', 3),
  type('float', 'single-precision float', 8),
  type('double', 'double-precision float', 6),
  type('string', 'string object', 5),
  type('array', 'array template type', 4),
  type('dictionary', 'dictionary object', 3),
  type('any', 'generic value container', 2),
];

export const ANGELSCRIPT_LITERAL_COMPLETIONS = [
  literal('true', 'boolean true', 8),
  literal('false', 'boolean false', 8),
  literal('null', 'null object handle', 4),
];

export const ANGELSCRIPT_BUILTIN_FUNCTION_COMPLETIONS = [
  fn('abs', 'absolute value', 'abs($0)', 3),
  fn('min', 'minimum value', 'min($0, )', 3),
  fn('max', 'maximum value', 'max($0, )', 3),
  fn('clamp', 'clamp value to range', 'clamp($0, minValue, maxValue)', 3),
  fn('sin', 'sine', 'sin($0)', 2),
  fn('cos', 'cosine', 'cos($0)', 2),
  fn('tan', 'tangent', 'tan($0)', 2),
  fn('asin', 'arc sine', 'asin($0)', 1),
  fn('acos', 'arc cosine', 'acos($0)', 1),
  fn('atan', 'arc tangent', 'atan($0)', 1),
  fn('atan2', 'arc tangent y/x', 'atan2($0, x)', 1),
  fn('sqrt', 'square root', 'sqrt($0)', 2),
  fn('pow', 'power', 'pow($0, exponent)', 2),
  fn('floor', 'round down', 'floor($0)', 1),
  fn('ceil', 'round up', 'ceil($0)', 1),
  fn('round', 'round to nearest', 'round($0)', 1),
  fn('log', 'natural logarithm', 'log($0)', 1),
  fn('log10', 'base-10 logarithm', 'log10($0)', 1),
];

export const ANGELSCRIPT_SNIPPET_COMPLETIONS = [
  snippet('void scan()', 'required PLC scan entry point', 'void scan()\n{\n\t$0\n}', [14, 16]),
  snippet('if', 'conditional block', 'if ($0)\n{\n\t\n}', [4, 4, 9, 9]),
  snippet('ifelse', 'if / else block', 'if ($0)\n{\n\t\n}\nelse\n{\n\t\n}', [4, 4, 9, 9]),
  snippet('for', 'for loop', 'for (int i = 0; i < $0; i++)\n{\n\t\n}', [21, 21, 30, 30]),
  snippet('while', 'while loop', 'while ($0)\n{\n\t\n}', [7, 7, 12, 12]),
  snippet('switch', 'switch statement', 'switch ($0)\n{\ncase 0:\n\tbreak;\ndefault:\n\tbreak;\n}', [8, 8]),
  snippet('class', 'class declaration', 'class $0\n{\npublic:\n\t\n}', [6, 6]),
  snippet('enum', 'enum declaration', 'enum $0\n{\n\tValue0,\n\tValue1\n}', [5, 5]),
  snippet('func', 'function declaration', 'void $0()\n{\n\t\n}', [5, 5]),
  snippet('guard', 'early return guard', 'if ($0) return;\n', [4, 4]),
];

export const PILAB_PLC_COMPLETIONS = [
  ...Array.from({ length: 16 }, (_, i) => variable(`I${i}`, 'digital input', 12)),
  ...Array.from({ length: 8 }, (_, i) => variable(`Q${i}`, 'digital output', 12)),
  ...Array.from({ length: 8 }, (_, i) => variable(`AI${i}`, 'analog input', 10)),
  ...Array.from({ length: 4 }, (_, i) => variable(`AO${i}`, 'analog output', 10)),

  // Common user/script names and PLC-style helpers. These are harmless even if
  // a given firmware build does not expose all helpers yet; they are just editor
  // suggestions and can be removed/extended as your AngelScript API grows.
  variable('counter', 'example variable', 1),
  variable('AutoMode', 'common user tag name', 2),
  variable('ManualMode', 'common user tag name', 2),
  variable('StartCmd', 'common command tag name', 2),
  variable('StopCmd', 'common command tag name', 2),
  variable('FaultActive', 'common status tag name', 2),
  variable('TankSetpoint', 'common setpoint tag name', 2),
  variable('PumpStart', 'common command tag name', 2),
];

export const ANGELSCRIPT_COMPLETIONS = [
  ...ANGELSCRIPT_SNIPPET_COMPLETIONS,
  ...ANGELSCRIPT_KEYWORD_COMPLETIONS,
  ...ANGELSCRIPT_TYPE_COMPLETIONS,
  ...ANGELSCRIPT_LITERAL_COMPLETIONS,
  ...ANGELSCRIPT_BUILTIN_FUNCTION_COMPLETIONS,
  ...PILAB_PLC_COMPLETIONS,
];

export function makeTagCompletions(tagNames = [], pointsByName = {}) {
  return tagNames.map((name) => {
    const p = pointsByName?.[name] || {};
    const type = p.type || p.value_type || typeof p.value;
    const source = p.source ? `${p.source} tag` : 'PLC tag';
    const units = p.units ? ` ${p.units}` : '';
    return {
      label: name,
      detail: `${source}${type && type !== 'undefined' ? ` · ${type}` : ''}${units}`,
      icon: /^Q\d+$/.test(name) || /^I\d+$/.test(name) || /^A[IO]\d+$/.test(name) ? 'variable' : 'property',
      boost: 20,
    };
  });
}

export function mergeCompletions(...groups) {
  const seen = new Set();
  const out = [];
  for (const group of groups) {
    for (const item of group || []) {
      if (!item?.label || seen.has(item.label)) continue;
      seen.add(item.label);
      out.push(item);
    }
  }
  return out;
}
