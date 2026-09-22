import fs from 'fs';
import path from 'path';
import { generateTerrainAuto, DEFAULT_CONFIG, recommendedGridSize } from './src/terrain.js';
import { exportHeightmap } from './src/export.js';
import { resolveConfig } from './src/presets.js';

function parseArgs(argv) {
  const opts = {
    preset: 'default',
    out: 'heightmap.txt',
    meta: 'meta.json',
    worldSize: DEFAULT_CONFIG.worldSize,
    gridSize: DEFAULT_CONFIG.gridSize,
    seed: DEFAULT_CONFIG.seed,
  };

  const args = [...argv];
  if (args.length > 0 && !args[0].startsWith('--')) {
    opts.preset = args.shift();
  }

  for (let i = 0; i < args.length; ++i) {
    const arg = args[i];
    if (arg === '--out' && args[i + 1]) opts.out = args[++i];
    else if (arg === '--meta' && args[i + 1]) opts.meta = args[++i];
    else if (arg === '--world-size' && args[i + 1]) opts.worldSize = Number(args[++i]);
    else if (arg === '--grid-size' && args[i + 1]) opts.gridSize = Number(args[++i]);
    else if (arg === '--seed' && args[i + 1]) opts.seed = Number(args[++i]);
  }

  if (!opts.gridSize || opts.gridSize < 2) {
    opts.gridSize = recommendedGridSize(opts.worldSize);
  }
  return opts;
}

const opts = parseArgs(process.argv.slice(2));

const config =
  opts.preset === 'blend'
    ? {
        ...DEFAULT_CONFIG,
        worldSize: opts.worldSize,
        gridSize: opts.gridSize,
        seed: opts.seed,
        blend: {
          enabled: true,
          presetA: 'alpine',
          presetB: 'mesa',
          mask: { type: 'noise', scale: 80, sharpness: 1.2 },
        },
      }
    : resolveConfig(
        { ...DEFAULT_CONFIG, worldSize: opts.worldSize, gridSize: opts.gridSize, seed: opts.seed },
        opts.preset
      );

const result = generateTerrainAuto(config);
exportHeightmap(result.heights, result.gridSize, opts.out);

const meta = {
  preset: opts.preset,
  gridSize: result.gridSize,
  worldSize: result.worldSize,
  seed: opts.seed,
  minHeight: result.stats.elevation.min,
  maxHeight: result.stats.elevation.max,
};

fs.mkdirSync(path.dirname(path.resolve(opts.meta)), { recursive: true });
fs.writeFileSync(opts.meta, JSON.stringify(meta, null, 2));
