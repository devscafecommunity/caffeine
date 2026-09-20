import { generateTerrainAuto, DEFAULT_CONFIG } from './src/terrain.js';
import { exportHeightsToOBJ, exportHeightmap } from './src/export.js';
import { resolveConfig } from './src/presets.js';

const preset = process.argv[2] || 'default';
const config =
  preset === 'blend'
    ? {
        ...DEFAULT_CONFIG,
        blend: {
          enabled: true,
          presetA: 'alpine',
          presetB: 'mesa',
          mask: { type: 'noise', scale: 80, sharpness: 1.2 },
        },
      }
    : resolveConfig(DEFAULT_CONFIG, preset);

console.log('═══════════════════════════════════════════════════');
console.log('  Ultra-Realistic Terrain Generator');
console.log('  Domain Warping + Geomorphological PDE');
console.log(`  Preset: ${preset}${config.blend?.enabled ? ` (${config.blend.presetA} ⊕ ${config.blend.presetB})` : ''}`);
console.log('═══════════════════════════════════════════════════\n');

const result = generateTerrainAuto(config);

exportHeightsToOBJ(result.heights, result.gridSize, result.worldSize, 'terrain.obj');
exportHeightmap(result.heights, result.gridSize, 'heightmap.txt');

console.log('\n── Statistics ──');
console.log(`  Elevation: ${result.stats.elevation.min.toFixed(2)} → ${result.stats.elevation.max.toFixed(2)} (μ=${result.stats.elevation.mean.toFixed(2)})`);
console.log(`  Temperature: ${result.stats.temperature.min.toFixed(1)}°C → ${result.stats.temperature.max.toFixed(1)}°C`);
console.log(`  Moisture: ${result.stats.moisture.min.toFixed(2)} → ${result.stats.moisture.max.toFixed(2)}`);
console.log('\n✓ Exported: terrain.obj, heightmap.txt');
console.log('  Run "npm run view" for interactive 3D visualization.\n');
