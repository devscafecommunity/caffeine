#!/usr/bin/env python3
"""
Editor Test Automation Framework for Caffeine Doppio

Provides automated testing of editor features without manual UI interaction.
Supports:
  - Launching Doppio with test parameters
  - Loading test scenes
  - Validating raycasting/selection
  - Testing multi-select, delete, focus
  - Collecting diagnostic logs
"""

import subprocess
import os
import sys
import json
import tempfile
import time
from pathlib import Path
from typing import Optional, Dict, List, Tuple
import logging

# Setup logging
logging.basicConfig(
    level=logging.DEBUG,
    format='[%(levelname)s] %(asctime)s - %(message)s'
)
logger = logging.getLogger(__name__)


class DoppioTestHarness:
    """Harness to run Doppio in test mode with instrumentation."""
    
    def __init__(self, doppio_bin: str, project_root: str):
        self.doppio_bin = doppio_bin
        self.project_root = Path(project_root)
        self.process = None
        self.test_results = {}
        
    def launch(self, scene_path: Optional[str] = None, headless: bool = True) -> bool:
        """
        Launch Doppio with optional scene preload.
        
        Args:
            scene_path: Path to .caf scene file to preload
            headless: If True, run without SDL rendering (console output only)
        
        Returns:
            True if launch successful, False otherwise
        """
        env = os.environ.copy()
        env['DOPPIO_TEST_MODE'] = '1'
        if headless:
            env['DOPPIO_HEADLESS'] = '1'
        
        cmd = [self.doppio_bin]
        if scene_path:
            cmd.extend(['--scene', str(scene_path)])
        
        logger.info(f"Launching: {' '.join(cmd)}")
        try:
            self.process = subprocess.Popen(
                cmd,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1
            )
            logger.info(f"Process started with PID {self.process.pid}")
            return True
        except Exception as e:
            logger.error(f"Failed to launch Doppio: {e}")
            return False
    
    def wait_for_output(self, pattern: str, timeout_secs: float = 10.0) -> bool:
        """
        Wait for specific log output pattern.
        
        Useful for waiting for "Scene loaded", "Viewport ready", etc.
        """
        logger.info(f"Waiting for pattern: '{pattern}' (timeout: {timeout_secs}s)")
        start = time.time()
        
        while self.process and self.process.poll() is None:
            if time.time() - start > timeout_secs:
                logger.warning(f"Timeout waiting for pattern: {pattern}")
                return False
            
            try:
                line = self.process.stdout.readline()
                if not line:
                    time.sleep(0.1)
                    continue
                
                if pattern in line:
                    logger.info(f"✓ Found pattern: {line.strip()}")
                    return True
                
                logger.debug(f"  {line.rstrip()}")
            except Exception as e:
                logger.error(f"Error reading output: {e}")
                return False
        
        logger.error("Process exited before pattern found")
        return False
    
    def send_test_command(self, command: str) -> bool:
        """
        Send test command to Doppio via stdin.
        
        Commands:
          - "select_entity <id>" - Click to select entity
          - "multi_select <id>" - Shift+click to add to selection
          - "delete_selected" - Press Delete key
          - "focus_selected" - Double-click to focus
          - "get_selected" - Query current selection
          - "get_scene" - Query scene entities
        """
        if not self.process or self.process.poll() is not None:
            logger.error("Process not running")
            return False
        
        logger.info(f"Sending command: {command}")
        try:
            self.process.stdin.write(command + '\n')
            self.process.stdin.flush()
            return True
        except Exception as e:
            logger.error(f"Failed to send command: {e}")
            return False
    
    def get_result(self, result_key: str, timeout_secs: float = 5.0) -> Optional[Dict]:
        """
        Wait for JSON result from Doppio.
        
        Expected format in stdout:
          TEST_RESULT: {"key": "value", "success": true}
        """
        logger.info(f"Waiting for result: {result_key}")
        start = time.time()
        
        while self.process and self.process.poll() is None:
            if time.time() - start > timeout_secs:
                logger.warning(f"Timeout waiting for result: {result_key}")
                return None
            
            try:
                line = self.process.stdout.readline()
                if not line:
                    time.sleep(0.1)
                    continue
                
                if "TEST_RESULT:" in line:
                    json_str = line.split("TEST_RESULT:", 1)[1].strip()
                    result = json.loads(json_str)
                    
                    if result.get('key') == result_key:
                        logger.info(f"✓ Result: {result}")
                        self.test_results[result_key] = result
                        return result
                
                logger.debug(f"  {line.rstrip()}")
            except json.JSONDecodeError as e:
                logger.warning(f"Invalid JSON in result: {e}")
            except Exception as e:
                logger.error(f"Error reading result: {e}")
        
        return None
    
    def terminate(self) -> bool:
        """Gracefully shutdown Doppio."""
        if self.process:
            logger.info("Terminating Doppio...")
            try:
                self.process.terminate()
                self.process.wait(timeout=5)
                logger.info("Process terminated")
                return True
            except subprocess.TimeoutExpired:
                logger.warning("Force killing process...")
                self.process.kill()
                return False
        return True
    
    def get_logs(self) -> Tuple[str, str]:
        """Retrieve stdout and stderr from process."""
        if self.process:
            stdout, stderr = self.process.communicate(timeout=2)
            return stdout, stderr
        return "", ""


class EditorTestSuite:
    """High-level test suite for editor features."""
    
    def __init__(self, harness: DoppioTestHarness):
        self.harness = harness
        self.passed = 0
        self.failed = 0
    
    def test_scene_load(self, scene_path: str) -> bool:
        """Test: Scene loads successfully."""
        logger.info("\n" + "="*60)
        logger.info("TEST: Scene Load")
        logger.info("="*60)
        
        if not self.harness.launch(scene_path):
            logger.error("✗ Failed to launch Doppio")
            self.failed += 1
            return False
        
        if not self.harness.wait_for_output("Scene loaded", timeout_secs=15):
            logger.error("✗ Scene did not load")
            self.failed += 1
            return False
        
        logger.info("✓ Scene loaded successfully")
        self.passed += 1
        return True
    
    def test_entity_selection(self, entity_id: int) -> bool:
        """Test: Raycasting selects entity."""
        logger.info("\n" + "="*60)
        logger.info(f"TEST: Entity Selection (ID: {entity_id})")
        logger.info("="*60)
        
        # Send command to select entity via raycasting
        if not self.harness.send_test_command(f"select_entity {entity_id}"):
            logger.error("✗ Failed to send select command")
            self.failed += 1
            return False
        
        # Query result
        result = self.harness.get_result("selected_entity")
        if not result or result.get('id') != entity_id:
            logger.error(f"✗ Entity not selected (expected {entity_id}, got {result})")
            self.failed += 1
            return False
        
        logger.info(f"✓ Entity {entity_id} selected successfully")
        self.passed += 1
        return True
    
    def test_multi_select(self, entity_ids: List[int]) -> bool:
        """Test: Multi-select with Shift+Click."""
        logger.info("\n" + "="*60)
        logger.info(f"TEST: Multi-Select (IDs: {entity_ids})")
        logger.info("="*60)
        
        # Select first entity
        if not self.harness.send_test_command(f"select_entity {entity_ids[0]}"):
            logger.error("✗ Failed to select first entity")
            self.failed += 1
            return False
        
        time.sleep(0.2)
        
        # Shift+click to add others
        for entity_id in entity_ids[1:]:
            if not self.harness.send_test_command(f"multi_select {entity_id}"):
                logger.error(f"✗ Failed to add entity {entity_id} to selection")
                self.failed += 1
                return False
            time.sleep(0.2)
        
        # Query result
        result = self.harness.get_result("selected_entities")
        if not result:
            logger.error("✗ Failed to get selection list")
            self.failed += 1
            return False
        
        selected = result.get('ids', [])
        if sorted(selected) != sorted(entity_ids):
            logger.error(f"✗ Selection mismatch (expected {entity_ids}, got {selected})")
            self.failed += 1
            return False
        
        logger.info(f"✓ Multi-select successful: {selected}")
        self.passed += 1
        return True
    
    def test_delete_selected(self, entity_id: int) -> bool:
        """Test: Delete key removes selected entity."""
        logger.info("\n" + "="*60)
        logger.info(f"TEST: Delete Selected (ID: {entity_id})")
        logger.info("="*60)
        
        # Select entity
        if not self.harness.send_test_command(f"select_entity {entity_id}"):
            logger.error("✗ Failed to select entity")
            self.failed += 1
            return False
        
        time.sleep(0.2)
        
        # Send delete command
        if not self.harness.send_test_command("delete_selected"):
            logger.error("✗ Failed to send delete command")
            self.failed += 1
            return False
        
        time.sleep(0.2)
        
        # Query scene to verify entity removed
        result = self.harness.get_result("scene_entities")
        if not result:
            logger.error("✗ Failed to get scene entities")
            self.failed += 1
            return False
        
        entity_ids = result.get('ids', [])
        if entity_id in entity_ids:
            logger.error(f"✗ Entity {entity_id} still exists after delete")
            self.failed += 1
            return False
        
        logger.info(f"✓ Entity {entity_id} deleted successfully")
        self.passed += 1
        return True
    
    def test_focus_camera(self, entity_id: int) -> bool:
        """Test: Double-click focuses camera on entity."""
        logger.info("\n" + "="*60)
        logger.info(f"TEST: Focus Camera (ID: {entity_id})")
        logger.info("="*60)
        
        # Select and double-click
        if not self.harness.send_test_command(f"select_entity {entity_id}"):
            logger.error("✗ Failed to select entity")
            self.failed += 1
            return False
        
        time.sleep(0.2)
        
        if not self.harness.send_test_command("focus_selected"):
            logger.error("✗ Failed to send focus command")
            self.failed += 1
            return False
        
        # Query camera focus
        result = self.harness.get_result("camera_state")
        if not result or result.get('success') != True:
            logger.error("✗ Camera focus failed")
            self.failed += 1
            return False
        
        logger.info(f"✓ Camera focused on entity {entity_id}")
        self.passed += 1
        return True
    
    def print_summary(self):
        """Print test run summary."""
        total = self.passed + self.failed
        logger.info("\n" + "="*60)
        logger.info("TEST SUMMARY")
        logger.info("="*60)
        logger.info(f"Passed: {self.passed}")
        logger.info(f"Failed: {self.failed}")
        logger.info(f"Total:  {total}")
        if total > 0:
            pass_rate = (self.passed / total) * 100
            logger.info(f"Pass Rate: {pass_rate:.1f}%")
        logger.info("="*60 + "\n")
        
        return self.failed == 0


def main():
    """Run editor test suite."""
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <doppio_binary> [scene_path]")
        sys.exit(1)
    
    doppio_bin = sys.argv[1]
    scene_path = sys.argv[2] if len(sys.argv) > 2 else None
    project_root = "/home/pedro/repo/caffeine"
    
    # Verify Doppio exists
    if not os.path.exists(doppio_bin):
        logger.error(f"Doppio binary not found: {doppio_bin}")
        sys.exit(1)
    
    # Create harness and test suite
    harness = DoppioTestHarness(doppio_bin, project_root)
    suite = EditorTestSuite(harness)
    
    try:
        # Test 1: Scene load
        if scene_path and os.path.exists(scene_path):
            suite.test_scene_load(scene_path)
        else:
            logger.warning("No scene path provided, skipping scene load test")
        
        # Test 2-5: Selection features (if scene loaded)
        if scene_path:
            suite.test_entity_selection(1)
            suite.test_multi_select([1, 2, 3])
            suite.test_delete_selected(4)
            suite.test_focus_camera(1)
        
        # Print results
        success = suite.print_summary()
        sys.exit(0 if success else 1)
        
    finally:
        harness.terminate()


if __name__ == '__main__':
    main()
