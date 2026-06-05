import fs from 'node:fs';
import path from 'node:path';

const dist = path.resolve('dist');
const indexPath = path.join(dist, 'index.html');
let html = fs.readFileSync(indexPath, 'utf8');

html = html.replace(/<script type="module" crossorigin src="([^"]+)"><\/script>/g, (_, src) => {
  const p = path.join(dist, src.replace(/^\//, ''));
  return `<script type="module">\n${fs.readFileSync(p, 'utf8')}\n<\/script>`;
});
html = html.replace(/<link rel="stylesheet" crossorigin href="([^"]+)">/g, (_, href) => {
  const p = path.join(dist, href.replace(/^\//, ''));
  return `<style>\n${fs.readFileSync(p, 'utf8')}\n<\/style>`;
});
fs.writeFileSync(path.join(dist, 'panel.html'), html);
console.log('Wrote dist/panel.html for firmware embedding');
