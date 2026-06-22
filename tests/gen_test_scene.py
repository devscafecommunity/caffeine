#!/usr/bin/env python3
"""
Generate a test scene fixture for editor automation.
Creates a minimal .caf scene with 3 cube entities for testing.
"""

import struct
import sys
from pathlib import Path

# Scene format constants (from SceneSerializer.hpp)
CAFF_SIGNATURE = 0x46464143  # "CAFF"
FORMAT_VERSION = 5

# Component type IDs
kTypeName = 0
kTypeTransform = 1
kTypePosition3D = 15
kTypeRotation3D = 16
kTypeScale3D = 17
kTypeMeshFilter = 18
kTypeMeshRenderer = 19

def write_string(f, s: str):
    """Write a UTF-8 string with u32 length prefix"""
    encoded = s.encode('utf-8')
    f.write(struct.pack('<I', len(encoded)))
    f.write(encoded)

def create_test_scene(output_path: str):
    """Create a minimal test scene with 3 cubes"""
    
    with open(output_path, 'wb') as f:
        # Write header
        f.write(struct.pack('<II', CAFF_SIGNATURE, FORMAT_VERSION))
        
        # Entity count (3 cubes)
        entity_count = 3
        f.write(struct.pack('<I', entity_count))
        
        # Entity 1: Cube at origin
        entity_id = 1
        f.write(struct.pack('<I', entity_id))
        
        # Component count: Name, Position3D, Rotation3D, Scale3D, MeshFilter, MeshRenderer
        component_count = 6
        f.write(struct.pack('<I', component_count))
        
        # Component 1: Name = "Cube1"
        f.write(struct.pack('<I', kTypeName))
        write_string(f, "Cube1")
        
        # Component 2: Position3D = (0, 0, 0)
        f.write(struct.pack('<I', kTypePosition3D))
        f.write(struct.pack('<III', 12, 0, 0))  # Size, x, y
        f.write(struct.pack('<f', 0.0))
        f.write(struct.pack('<f', 0.0))
        f.write(struct.pack('<f', 0.0))
        
        # Component 3: Rotation3D = (0, 0, 0, 1) [identity quat]
        f.write(struct.pack('<I', kTypeRotation3D))
        f.write(struct.pack('<I', 16))  # Size
        f.write(struct.pack('<ffff', 0.0, 0.0, 0.0, 1.0))
        
        # Component 4: Scale3D = (1, 1, 1)
        f.write(struct.pack('<I', kTypeScale3D))
        f.write(struct.pack('<I', 12))  # Size
        f.write(struct.pack('<fff', 1.0, 1.0, 1.0))
        
        # Component 5: MeshFilter (primitive cube)
        f.write(struct.pack('<I', kTypeMeshFilter))
        write_string(f, "Cube")  # Primitive type
        
        # Component 6: MeshRenderer
        f.write(struct.pack('<I', kTypeMeshRenderer))
        f.write(struct.pack('<I', 0))  # Empty material reference
        
        # Entity 2: Cube at (2, 0, 0)
        entity_id = 2
        f.write(struct.pack('<I', entity_id))
        f.write(struct.pack('<I', component_count))
        
        f.write(struct.pack('<I', kTypeName))
        write_string(f, "Cube2")
        
        f.write(struct.pack('<I', kTypePosition3D))
        f.write(struct.pack('<I', 12))
        f.write(struct.pack('<fff', 2.0, 0.0, 0.0))
        
        f.write(struct.pack('<I', kTypeRotation3D))
        f.write(struct.pack('<I', 16))
        f.write(struct.pack('<ffff', 0.0, 0.0, 0.0, 1.0))
        
        f.write(struct.pack('<I', kTypeScale3D))
        f.write(struct.pack('<I', 12))
        f.write(struct.pack('<fff', 1.0, 1.0, 1.0))
        
        f.write(struct.pack('<I', kTypeMeshFilter))
        write_string(f, "Cube")
        
        f.write(struct.pack('<I', kTypeMeshRenderer))
        f.write(struct.pack('<I', 0))
        
        # Entity 3: Cube at (4, 0, 0)
        entity_id = 3
        f.write(struct.pack('<I', entity_id))
        f.write(struct.pack('<I', component_count))
        
        f.write(struct.pack('<I', kTypeName))
        write_string(f, "Cube3")
        
        f.write(struct.pack('<I', kTypePosition3D))
        f.write(struct.pack('<I', 12))
        f.write(struct.pack('<fff', 4.0, 0.0, 0.0))
        
        f.write(struct.pack('<I', kTypeRotation3D))
        f.write(struct.pack('<I', 16))
        f.write(struct.pack('<ffff', 0.0, 0.0, 0.0, 1.0))
        
        f.write(struct.pack('<I', kTypeScale3D))
        f.write(struct.pack('<I', 12))
        f.write(struct.pack('<fff', 1.0, 1.0, 1.0))
        
        f.write(struct.pack('<I', kTypeMeshFilter))
        write_string(f, "Cube")
        
        f.write(struct.pack('<I', kTypeMeshRenderer))
        f.write(struct.pack('<I', 0))
    
    print(f"✓ Created test scene: {output_path}")

if __name__ == '__main__':
    output = sys.argv[1] if len(sys.argv) > 1 else '/home/pedro/repo/caffeine/tests/fixtures/test_scene_multiobject.caf'
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    create_test_scene(output)
