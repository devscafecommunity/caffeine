import { initialHeight } from './noise.js';

export function buildHeightGrid(size, worldSize, noiseParams, noiseGenerators) {
  const { noiseWarpX, noiseWarpY, noiseBase } = noiseGenerators;
  const heights = new Float32Array(size * size);
  const half = worldSize / 2;
  const cellSize = worldSize / (size - 1);

  for (let z = 0; z < size; z++) {
    for (let x = 0; x < size; x++) {
      const worldX = x * cellSize - half;
      const worldZ = z * cellSize - half;
      const h = initialHeight(worldX, worldZ, noiseParams, noiseWarpX, noiseWarpY, noiseBase);
      heights[z * size + x] = h;
    }
  }

  return { heights, cellSize };
}
