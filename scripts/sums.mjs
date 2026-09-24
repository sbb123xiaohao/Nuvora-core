// Regenerate SHA256SUMS with the same file selection as scripts/package.py.
import { createHash } from 'node:crypto';
import { readdirSync, statSync, readFileSync, writeFileSync } from 'node:fs';
import { join, relative, sep } from 'node:path';

const root = new URL('..', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1');
const VERSION = '0.8.0';

function walk(dir, out = []) {
  for (const entry of readdirSync(dir).sort()) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) walk(full, out);
    else out.push(full);
  }
  return out;
}

const selected = [];
for (const path of walk(root)) {
  const rel = relative(root, path).split(sep);
  if (rel.join('/') === 'SHA256SUMS') continue;
  if (rel.includes('__pycache__') || rel.some((p) => p.endsWith('.pyc'))) continue;
  if (rel[0] === 'build') {
    if (rel.length < 3 || !['x86_64', 'aarch64'].includes(rel[1])) continue;
    const tail = rel.slice(2);
    const tailName = tail[tail.length - 1];
    let keep = tail.length === 1 &&
      (['boot.elf', 'nuvora.elf', 'nuvora.map'].includes(tailName) ||
       (rel[1] === 'aarch64' && tailName === 'Image') ||
       (rel[1] === 'x86_64' && tailName === `nuvora-core-${VERSION}-x86_64.iso`));
    keep ||= tail.length === 1 && rel[1] === 'x86_64' &&
      ['BOOTX64.EFI', 'esp.img', `nuvora-core-${VERSION}-x86_64-uefi.iso`].includes(tailName);
    keep ||= tail.length === 2 && tail[0] === 'apps' && tailName.endsWith('.elf');
    keep ||= tail.length === 2 && tail[0] === 'test-results' &&
      ['.log', '.json', '.md', '.png'].some((ext) => tailName.endsWith(ext));
    if (!keep) continue;
  }
  selected.push(path);
}

const lines = selected.map((path) => {
  const digest = createHash('sha256').update(readFileSync(path)).digest('hex');
  return `${digest}  ${relative(root, path).split(sep).join('/')}`;
});
writeFileSync(join(root, 'SHA256SUMS'), lines.join('\n') + '\n');
console.log(`SHA256SUMS regenerated: ${lines.length} files`);
