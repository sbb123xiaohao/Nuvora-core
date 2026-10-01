// Match package.py's complete committed source selection, without build data.
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';

const root = fileURLToPath(new URL('../', import.meta.url));
execFileSync(process.env.PYTHON || 'python3',
  [join(root, 'scripts/package.py'), '--manifest', join(root, 'SHA256SUMS')],
  { cwd: root, stdio: 'inherit' });
