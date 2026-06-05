import Prism from 'prismjs';
import 'prismjs/themes/prism-tomorrow.css';

Prism.languages.angelscript = {
  comment: [/\/\/.*|\/\*[\s\S]*?\*\//],
  string: /"(?:\\.|[^"\\])*"/,
  keyword: /\b(?:void|bool|float|double|int|string|if|else|for|while|return|true|false|const|class|enum|namespace)\b/,
  number: /\b\d+(?:\.\d+)?f?\b/,
  function: /\b[A-Za-z_]\w*(?=\s*\()/,
  operator: /[+\-*\/=%!<>]=?|&&|\|\|/,
  punctuation: /[{}[\];(),.:]/
};
