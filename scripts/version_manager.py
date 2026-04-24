#!/usr/bin/env python3
"""
Version Manager for Caffeine Engine
Manages semantic versioning and metadata for built binaries
"""

import sys
import os
import json
import argparse
from datetime import datetime
from pathlib import Path

# Add scripts directory to path to import build_manager
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_manager import BuildManager


class VersionManager:
    """Manages semantic versioning for binaries"""
    
    def __init__(self, build_type="Debug"):
        self.build_type = build_type
        self.build_manager = BuildManager()
        self.version_dir = Path(".versions")
        self.version_file = self.version_dir / f"{build_type}_version.json"
        self._ensure_version_dir()
        
    def _ensure_version_dir(self):
        """Create .versions directory if it doesn't exist"""
        self.version_dir.mkdir(exist_ok=True)
        
    def _load_version(self):
        """Load version data from JSON file"""
        if not self.version_file.exists():
            return {
                "major": 0,
                "minor": 1,
                "patch": 0,
                "build": 0,
                "timestamp": datetime.now().isoformat(),
                "git_commit": "",
                "git_branch": ""
            }
        
        with open(self.version_file, 'r') as f:
            return json.load(f)
    
    def _save_version(self, version_data):
        """Save version data to JSON file"""
        with open(self.version_file, 'w') as f:
            json.dump(version_data, f, indent=2)
    
    def _get_git_info(self):
        """Get current git commit and branch"""
        try:
            import subprocess
            commit = subprocess.check_output(
                ["git", "rev-parse", "--short", "HEAD"],
                stderr=subprocess.DEVNULL
            ).decode().strip()
            
            branch = subprocess.check_output(
                ["git", "rev-parse", "--abbrev-ref", "HEAD"],
                stderr=subprocess.DEVNULL
            ).decode().strip()
            
            return commit, branch
        except:
            return "unknown", "unknown"
    
    def bump_version(self, component="patch"):
        """Increment version component (major/minor/patch)"""
        version = self._load_version()
        
        if component == "major":
            version["major"] += 1
            version["minor"] = 0
            version["patch"] = 0
        elif component == "minor":
            version["minor"] += 1
            version["patch"] = 0
        elif component == "patch":
            version["patch"] += 1
        else:
            raise ValueError(f"Invalid component: {component}")
        
        version["build"] += 1
        version["timestamp"] = datetime.now().isoformat()
        version["git_commit"], version["git_branch"] = self._get_git_info()
        
        self._save_version(version)
        return version
    
    def show_version(self):
        """Display current version information"""
        version = self._load_version()
        
        print(f"\n{'='*60}")
        print(f"  {self.build_type} Build Version")
        print(f"{'='*60}")
        print(f"  Version:     {version['major']}.{version['minor']}.{version['patch']}")
        print(f"  Build:       #{version['build']}")
        print(f"  Timestamp:   {version['timestamp']}")
        print(f"  Git Commit:  {version['git_commit']}")
        print(f"  Git Branch:  {version['git_branch']}")
        print(f"{'='*60}\n")
        
        return version
    
    def export_version(self, output_file):
        """Generate C++ header file with version constants"""
        version = self._load_version()
        
        header_content = f"""// Auto-generated version header for {self.build_type} build
// Generated: {datetime.now().isoformat()}

#pragma once

#define CF_VERSION_MAJOR {version['major']}
#define CF_VERSION_MINOR {version['minor']}
#define CF_VERSION_PATCH {version['patch']}
#define CF_VERSION_BUILD {version['build']}

#define CF_VERSION_STRING "{version['major']}.{version['minor']}.{version['patch']}"
#define CF_VERSION_FULL "{version['major']}.{version['minor']}.{version['patch']}.{version['build']}"

#define CF_BUILD_TYPE "{self.build_type}"
#define CF_BUILD_TIMESTAMP "{version['timestamp']}"
#define CF_GIT_COMMIT "{version['git_commit']}"
#define CF_GIT_BRANCH "{version['git_branch']}"
"""
        
        output_path = Path(output_file)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        
        with open(output_path, 'w') as f:
            f.write(header_content)
        
        print(f"[OK] Version header exported to: {output_file}")
        return output_path


def main():
    parser = argparse.ArgumentParser(description="Caffeine Engine Version Manager")
    parser.add_argument("--type", choices=["Debug", "Release"], default="Debug",
                       help="Build type (default: Debug)")
    
    subparsers = parser.add_subparsers(dest="command", help="Available commands")
    
    # bump command
    bump_parser = subparsers.add_parser("bump", help="Increment version")
    bump_parser.add_argument("component", choices=["major", "minor", "patch"],
                            help="Version component to increment")
    
    # show command
    subparsers.add_parser("show", help="Display current version")
    
    # export command
    export_parser = subparsers.add_parser("export", help="Export version to C++ header")
    export_parser.add_argument("--output", required=True, help="Output header file path")
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
    
    vm = VersionManager(args.type)
    
    if args.command == "bump":
        version = vm.bump_version(args.component)
        print(f"[OK] Bumped {args.component} version to {version['major']}.{version['minor']}.{version['patch']}")
    elif args.command == "show":
        vm.show_version()
    elif args.command == "export":
        vm.export_version(args.output)
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
