import os
import sys
import json
import shutil
from pathlib import Path
import subprocess

# Add scripts to path
sys.path.insert(0, str(Path(__file__).parent.parent / "scripts"))

from build_manager import BuildManager, BuildConfig

def test_build_config_creation():
    """Test BuildConfig initialization"""
    config = BuildConfig(
        build_type="Debug",
        generator="Unix Makefiles",
        preset="development"
    )
    assert config.build_type == "Debug"
    assert config.generator == "Unix Makefiles"

def test_build_manager_init():
    """Test BuildManager initialization"""
    manager = BuildManager()
    assert manager.project_root is not None
    assert manager.build_dir.exists()

def test_version_file_creation():
    """Test version file creation and parsing"""
    manager = BuildManager()
    version_file = manager.get_version_file()
    assert version_file.suffix == ".json"

if __name__ == "__main__":
    test_build_config_creation()
    test_build_manager_init()
    test_version_file_creation()
    print("All tests passed!")
