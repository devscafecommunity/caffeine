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
    def _find_project_root(self, start_path: Path) -> Path:
        current = start_path
        for _ in range(5):
            if (current / "CMakeLists.txt").exists():
                return current
            parent = current.parent
            if parent == current:
                break
            current = parent
        return start_path
    
    def __init__(self, project_root: Optional[Path] = None):
        if project_root is None:
            start_path = Path(__file__).parent.parent.resolve()
            self.project_root = self._find_project_root(start_path)
        else:
            self.project_root = Path(project_root).resolve()
        
        self.build_dir = self.project_root / "build"
        self.bin_dir = self.project_root / "bin"
        self.scripts_dir = self.project_root / "scripts"
        self.versions_dir = self.project_root / ".versions"
        
        self.build_dir.mkdir(exist_ok=True)
        self.bin_dir.mkdir(exist_ok=True)
        self.scripts_dir.mkdir(exist_ok=True)
        self.versions_dir.mkdir(exist_ok=True)
        
        self.cmake_cache = self.build_dir / "CMakeCache.txt"
    
    def get_version_file(self, build_type: str = "Debug") -> Path:
        return self.versions_dir / f"{build_type.lower()}_version.json"
    
    def read_version_info(self, build_type: str = "Debug") -> Dict:
        version_file = self.get_version_file(build_type)
        if version_file.exists():
            with open(version_file, 'r') as f:
                return json.load(f)
        return {}
    
    def write_version_info(self, version_info: Dict, build_type: str = "Debug"):
        version_file = self.get_version_file(build_type)
        with open(version_file, 'w') as f:
            json.dump(version_info, f, indent=2)
    
    def configure(self, config: BuildConfig) -> bool:
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
            self.write_version_info(version_info, config.build_type)
        
        return result.returncode == 0
    
    def build(self, config: BuildConfig, targets: Optional[List[str]] = None) -> bool:
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
        return result.returncode == 0
    
    def clean(self) -> bool:
        args = [
            "cmake",
            "--build", str(self.build_dir),
            "--target", "clean"
        ]
        
        print(f"Running: {' '.join(args)}")
        result = subprocess.run(args, cwd=self.project_root)
        return result.returncode == 0
    
    def run_tests(self, config: BuildConfig) -> bool:
        test_dir = self.build_dir / "tests"
        if not test_dir.exists():
            print("No test directory found, skipping tests")
            return True
        
        args = ["ctest", "--output-on-failure", "-C", config.build_type]
        
        print(f"Running: {' '.join(args)}")
        result = subprocess.run(args, cwd=self.build_dir)
        return result.returncode == 0
    
    def get_git_commit(self) -> str:
        try:
            result = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=self.project_root,
                capture_output=True,
                text=True,
                check=True
            )
            return result.stdout.strip()
        except subprocess.CalledProcessError:
            return ""
    
    def get_git_branch(self) -> str:
        try:
            result = subprocess.run(
                ["git", "rev-parse", "--abbrev-ref", "HEAD"],
                cwd=self.project_root,
                capture_output=True,
                text=True,
                check=True
            )
            return result.stdout.strip()
        except subprocess.CalledProcessError:
            return ""
    
    def list_binaries(self) -> List[Path]:
        binaries = []
        if self.bin_dir.exists():
            for item in self.bin_dir.iterdir():
                if item.is_file():
                    binaries.append(item)
        return binaries
