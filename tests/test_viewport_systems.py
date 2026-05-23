#!/usr/bin/env python3
"""
Viewport System Verification Tests
Tests all viewport systems: raycasting, selection, multi-select, delete, focus camera
"""

import subprocess
import json
import time
import sys
import logging
from pathlib import Path
from typing import Optional, Dict, Any

logging.basicConfig(
    level=logging.DEBUG,
    format='[%(levelname)s] %(asctime)s - %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger(__name__)

class ViewportSystemTest:
    """Test viewport systems end-to-end"""
    
    def __init__(self, doppio_binary: str, scene_path: str):
        self.doppio_binary = doppio_binary
        self.scene_path = scene_path
        self.process: Optional[subprocess.Popen] = None
        self.results = {
            'passed': 0,
            'failed': 0,
            'errors': []
        }
    
    def start_doppio(self) -> bool:
        """Start Doppio process with test mode enabled"""
        try:
            env = {'DOPPIO_TEST_MODE': '1', 'DOPPIO_HEADLESS': '1'}
            cmd = [self.doppio_binary, '--scene', self.scene_path]
            
            logger.info(f"Starting: {' '.join(cmd)}")
            self.process = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
                env={**Path(self.doppio_binary).cwd() and dict(os.environ) or dict(os.environ), **env}
            )
            logger.info(f"Process started with PID {self.process.pid}")
            time.sleep(1)
            return True
        except Exception as e:
            logger.error(f"Failed to start Doppio: {e}")
            self.results['errors'].append(f"Start failed: {e}")
            return False
    
    def send_command(self, cmd: str) -> bool:
        """Send command to Doppio via stdin"""
        if not self.process:
            logger.error("Process not running")
            return False
        
        try:
            logger.debug(f"Sending: {cmd}")
            self.process.stdin.write(cmd + '\n')
            self.process.stdin.flush()
            return True
        except Exception as e:
            logger.error(f"Failed to send command: {e}")
            self.results['errors'].append(f"Command failed: {e}")
            return False
    
    def read_output(self, timeout: float = 5.0) -> Optional[str]:
        """Read output from Doppio"""
        if not self.process:
            return None
        
        try:
            start = time.time()
            output = ""
            while time.time() - start < timeout:
                try:
                    line = self.process.stdout.readline()
                    if line:
                        output += line
                        if 'TEST_RESULT:' in line:
                            return output
                except:
                    pass
                time.sleep(0.01)
            return output if output else None
        except Exception as e:
            logger.error(f"Failed to read output: {e}")
            return None
    
    def parse_result(self, output: str) -> Optional[Dict[str, Any]]:
        """Parse TEST_RESULT JSON from output"""
        try:
            for line in output.split('\n'):
                if 'TEST_RESULT:' in line:
                    json_str = line.split('TEST_RESULT:')[1].strip()
                    return json.loads(json_str)
        except Exception as e:
            logger.debug(f"Failed to parse result: {e}")
        return None
    
    def test_scene_load(self) -> bool:
        """Test 1: Scene loads successfully"""
        logger.info("\n" + "="*60)
        logger.info("TEST 1: Scene Load")
        logger.info("="*60)
        
        try:
            time.sleep(2)
            
            if not self.send_command("get_scene"):
                logger.error("✗ Failed to send get_scene")
                self.results['failed'] += 1
                return False
            
            output = self.read_output(timeout=3)
            if output and 'scene_entities' in output:
                logger.info("✓ Scene loaded successfully")
                self.results['passed'] += 1
                return True
            else:
                logger.error("✗ Scene did not load or response not received")
                self.results['failed'] += 1
                return False
        except Exception as e:
            logger.error(f"✗ Test failed: {e}")
            self.results['failed'] += 1
            return False
    
    def test_entity_selection(self) -> bool:
        """Test 2: Click-to-select entity"""
        logger.info("\n" + "="*60)
        logger.info("TEST 2: Entity Selection (ID: 1)")
        logger.info("="*60)
        
        try:
            if not self.send_command("select_entity 1"):
                logger.error("✗ Failed to send select_entity command")
                self.results['failed'] += 1
                return False
            
            output = self.read_output(timeout=3)
            result = self.parse_result(output) if output else None
            
            if result and result.get('key') == 'selected_entity' and result.get('id') == 1:
                logger.info(f"✓ Entity 1 selected: {result}")
                self.results['passed'] += 1
                return True
            else:
                logger.error(f"✗ Selection failed or unexpected result: {result}")
                self.results['failed'] += 1
                return False
        except Exception as e:
            logger.error(f"✗ Test failed: {e}")
            self.results['failed'] += 1
            return False
    
    def test_multi_select(self) -> bool:
        """Test 3: Multi-select with Shift+Click"""
        logger.info("\n" + "="*60)
        logger.info("TEST 3: Multi-Select (Shift+Click)")
        logger.info("="*60)
        
        try:
            if not self.send_command("select_entity 1"):
                logger.error("✗ Failed to select entity 1")
                self.results['failed'] += 1
                return False
            
            time.sleep(0.5)
            
            if not self.send_command("multi_select 2"):
                logger.error("✗ Failed to multi-select entity 2")
                self.results['failed'] += 1
                return False
            
            output = self.read_output(timeout=3)
            result = self.parse_result(output) if output else None
            
            if result and result.get('key') == 'selected_entities':
                ids = result.get('ids', [])
                if 1 in ids and 2 in ids:
                    logger.info(f"✓ Multi-select works: {ids}")
                    self.results['passed'] += 1
                    return True
                else:
                    logger.error(f"✗ Selected IDs incorrect: {ids}")
                    self.results['failed'] += 1
                    return False
            else:
                logger.error(f"✗ Multi-select failed: {result}")
                self.results['failed'] += 1
                return False
        except Exception as e:
            logger.error(f"✗ Test failed: {e}")
            self.results['failed'] += 1
            return False
    
    def test_delete(self) -> bool:
        """Test 4: Delete selected entity"""
        logger.info("\n" + "="*60)
        logger.info("TEST 4: Delete Selected Entity")
        logger.info("="*60)
        
        try:
            if not self.send_command("select_entity 3"):
                logger.error("✗ Failed to select entity 3")
                self.results['failed'] += 1
                return False
            
            time.sleep(0.5)
            
            if not self.send_command("delete_selected"):
                logger.error("✗ Failed to send delete_selected")
                self.results['failed'] += 1
                return False
            
            output = self.read_output(timeout=3)
            result = self.parse_result(output) if output else None
            
            if result and result.get('key') == 'deleted':
                logger.info(f"✓ Entity deleted: {result}")
                self.results['passed'] += 1
                return True
            else:
                logger.error(f"✗ Delete failed: {result}")
                self.results['failed'] += 1
                return False
        except Exception as e:
            logger.error(f"✗ Test failed: {e}")
            self.results['failed'] += 1
            return False
    
    def test_focus_camera(self) -> bool:
        """Test 5: Focus camera on selected entity"""
        logger.info("\n" + "="*60)
        logger.info("TEST 5: Focus Camera on Entity")
        logger.info("="*60)
        
        try:
            if not self.send_command("select_entity 1"):
                logger.error("✗ Failed to select entity 1")
                self.results['failed'] += 1
                return False
            
            time.sleep(0.5)
            
            if not self.send_command("focus_selected"):
                logger.error("✗ Failed to send focus_selected")
                self.results['failed'] += 1
                return False
            
            output = self.read_output(timeout=3)
            result = self.parse_result(output) if output else None
            
            if result and result.get('key') == 'camera_state' and result.get('success'):
                logger.info(f"✓ Camera focused: {result}")
                self.results['passed'] += 1
                return True
            else:
                logger.error(f"✗ Focus failed: {result}")
                self.results['failed'] += 1
                return False
        except Exception as e:
            logger.error(f"✗ Test failed: {e}")
            self.results['failed'] += 1
            return False
    
    def stop_doppio(self):
        """Stop Doppio process"""
        if self.process:
            try:
                self.process.terminate()
                self.process.wait(timeout=5)
                logger.info("Process terminated")
            except:
                self.process.kill()
                logger.warning("Process force-killed")
    
    def run_all_tests(self) -> bool:
        """Run all tests"""
        logger.info(f"Starting viewport system verification tests")
        logger.info(f"Doppio: {self.doppio_binary}")
        logger.info(f"Scene: {self.scene_path}")
        
        if not self.start_doppio():
            return False
        
        try:
            self.test_scene_load()
            time.sleep(1)
            
            self.test_entity_selection()
            time.sleep(1)
            
            self.test_multi_select()
            time.sleep(1)
            
            self.test_delete()
            time.sleep(1)
            
            self.test_focus_camera()
        finally:
            self.stop_doppio()
        
        logger.info("\n" + "="*60)
        logger.info("TEST SUMMARY")
        logger.info("="*60)
        total = self.results['passed'] + self.results['failed']
        pass_rate = (self.results['passed'] / total * 100) if total > 0 else 0
        logger.info(f"Passed: {self.results['passed']}")
        logger.info(f"Failed: {self.results['failed']}")
        logger.info(f"Total:  {total}")
        logger.info(f"Pass Rate: {pass_rate:.1f}%")
        
        if self.results['errors']:
            logger.info("\nErrors encountered:")
            for error in self.results['errors']:
                logger.info(f"  - {error}")
        
        logger.info("="*60)
        
        return self.results['failed'] == 0

if __name__ == '__main__':
    import os
    
    doppio = sys.argv[1] if len(sys.argv) > 1 else '/home/pedro/repo/caffeine/build/doppio'
    scene = sys.argv[2] if len(sys.argv) > 2 else '/home/pedro/repo/caffeine/tests/fixtures/test_scene_multiobject.caf'
    
    tester = ViewportSystemTest(doppio, scene)
    success = tester.run_all_tests()
    sys.exit(0 if success else 1)
