import * as esbuild from 'esbuild';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(__dirname, '..');

const shared = {
  bundle: true,
  format: 'esm',
  platform: 'browser',
  logLevel: 'info',
};

await esbuild.build({
  ...shared,
  entryPoints: [path.join(root, 'src/client/terrainClient.js')],
  outfile: path.join(root, 'public/dist/terrain-gen.js'),
  alias: { three: path.join(root, 'src/threeStub.js') },
});

await esbuild.build({
  ...shared,
  entryPoints: [path.join(root, 'src/client/presetsClient.js')],
  outfile: path.join(root, 'public/dist/presets-gen.js'),
});

console.log('✓ Client bundles written to public/dist/');
