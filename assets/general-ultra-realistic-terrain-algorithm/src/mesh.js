import * as THREE from 'three';
import { buildHeightGrid } from './heightGrid.js';

export { buildHeightGrid } from './heightGrid.js';

export function createTerrainGeometry(heights, size, worldSize, biomeColors = null) {
  const geometry = new THREE.PlaneGeometry(worldSize, worldSize, size - 1, size - 1);
  geometry.rotateX(-Math.PI / 2);

  const positions = geometry.attributes.position;
  for (let z = 0; z < size; z++) {
    for (let x = 0; x < size; x++) {
      const i = z * size + x;
      positions.setY(i, heights[i]);
    }
  }

  if (biomeColors) {
    geometry.setAttribute('color', new THREE.BufferAttribute(biomeColors, 3));
  }

  positions.needsUpdate = true;
  geometry.computeVertexNormals();
  return geometry;
}

export function heightsToPositionArray(heights, size, worldSize) {
  const half = worldSize / 2;
  const cellSize = worldSize / (size - 1);
  const positions = new Float32Array(size * size * 3);

  for (let z = 0; z < size; z++) {
    for (let x = 0; x < size; x++) {
      const i = z * size + x;
      positions[i * 3] = x * cellSize - half;
      positions[i * 3 + 1] = heights[i];
      positions[i * 3 + 2] = z * cellSize - half;
    }
  }

  return positions;
}
