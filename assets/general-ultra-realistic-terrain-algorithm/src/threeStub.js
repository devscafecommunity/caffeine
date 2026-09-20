/** Stub para bundle browser — geometria é construída no viewer Three.js */
export class BufferAttribute {
  constructor() {}
}

export class PlaneGeometry {
  constructor() {
    this.attributes = { position: { setY() {}, needsUpdate: false } };
  }
  rotateX() {}
  setAttribute() {}
  computeVertexNormals() {}
}
