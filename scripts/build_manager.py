import os
import sys
import json
import platform
import subprocess
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional, Dict, List

@dataclass
class BuildConfig:
    build_type: str = "Debug"
    generator: str = ""
    preset: Optional[str] = None
    additional_args: List[str] = field(default_factory=list)
    
    def __post_init__(self):
        
        if not self.generator:
            system = platform.system()
            if system == "Windows":
                self.generator = "Visual Studio 17 2022"
            elif system == "Darwin":
                self.generator = "Xcode"
            else:
                self.generator = "Unix Makefiles"

class BuildManager:
    def __init__(self, project_root: Optional[Path] = None):
        if project_root is None:
            self.project_root = Path(__file__).parent.parent.resolve()
        else:
            self.project_root = Path(project_root).resolve()
        
        self.build_dir = self.project_root / "build"
        self.build_dir.mkdir(exist_ok=True)
        
        self.cmake_cache = self.build_dir / "CMakeCache.txt"
        self.version_file = self.build_dir / "build_version.json"
    
    def get_version_file(self) -> Path:
        return self.version_file
    
    def read_version_info(self) -> Dict:
        if self.version_file.exists():
            with open(self.version_file, 'r') as f:
                return json.load(f)
        return {}
    
    def write_version_info(self, version_info: Dict):
        with open(self.version_file, 'w') as f:
            json.dump(version_info, f, indent=2)
    
    def get_current_config(self) -> Optional[BuildConfig]:
        version_info = self.read_version_info()
        if not version_info:
            return None
        
        return BuildConfig(
            build_type=version_info.get("build_type", "Debug"),
            generator=version_info.get("generator", ""),
            preset=version_info.get("preset"),
            additional_args=version_info.get("additional_args", [])
        )
    
    def needs_reconfigure(self, new_config: BuildConfig) -> bool:
        if not self.cmake_cache.exists():
            return True
        
        current_config = self.get_current_config()
        if current_config is None:
            return True
        
        return (
            current_config.build_type != new_config.build_type or
            current_config.generator != new_config.generator or
            current_config.preset != new_config.preset
        )
    
    def configure(self, config: BuildConfig) -> int:
        args = ["cmake"]
        
        if config.preset:
            args.extend(["--preset", config.preset])
        else:
            args.extend([
                "-S", str(self.project_root),
                "-B", str(self.build_dir),
                "-G", config.generator,
                f"-DCMAKE_BUILD_TYPE={config.build_type}"
            ])
        
        args.extend(config.additional_args)
        
        print(f"Running: {' '.join(args)}")
        result = subprocess.run(args, cwd=self.project_root)
        
        if result.returncode == 0:
            version_info = {
                "build_type": config.build_type,
                "generator": config.generator,
                "preset": config.preset,
                "additional_args": config.additional_args
            }
            self.write_version_info(version_info)
        
        return result.returncode
    
    def build(self, config: BuildConfig, targets: Optional[List[str]] = None) -> int:
        if self.needs_reconfigure(config):
            print("Configuration changed, reconfiguring...")
            ret = self.configure(config)
            if ret != 0:
                return ret
        
        args = [
            "cmake",
            "--build", str(self.build_dir),
            "--config", config.build_type
        ]
        
        if targets:
            for target in targets:
                args.extend(["--target", target])
        
        print(f"Running: {' '.join(args)}")
        result = subprocess.run(args, cwd=self.project_root)
        return result.returncode
    
    def clean(self) -> int:
        args = [
            "cmake",
            "--build", str(self.build_dir),
            "--target", "clean"
        ]
        
        print(f"Running: {' '.join(args)}")
        result = subprocess.run(args, cwd=self.project_root)
        return result.returncode
    
    def rebuild(self, config: BuildConfig) -> int:
        self.clean()
        return self.build(config)
    
    def run_tests(self, config: BuildConfig) -> int:
        test_dir = self.build_dir / "tests"
        if not test_dir.exists():
            print("No test directory found, skipping tests")
            return 0
        
        args = ["ctest", "--output-on-failure", "-C", config.build_type]
        
        print(f"Running: {' '.join(args)}")
        result = subprocess.run(args, cwd=self.build_dir)
        return result.returncode
    
    def get_platform_info(self) -> Dict[str, str]:
        return {
            "system": platform.system(),
            "machine": platform.machine(),
            "python_version": platform.python_version(),
            "project_root": str(self.project_root),
            "build_dir": str(self.build_dir)
        }
