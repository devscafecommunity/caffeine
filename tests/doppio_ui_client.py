#!/usr/bin/env python3

import subprocess
import json
import time
import sys
import logging
from pathlib import Path
from typing import Optional, Dict, Any, List

logging.basicConfig(
    level=logging.INFO,
    format='[%(levelname)s] %(message)s'
)
logger = logging.getLogger(__name__)

class DoppioUIElement:
    def __init__(self, data: Dict[str, Any]):
        self.id = data.get('id', 0)
        self.name = data.get('name', '')
        self.x = data.get('x', 0)
        self.y = data.get('y', 0)
        self.w = data.get('w', 0)
        self.h = data.get('h', 0)
        self.selected = data.get('selected', False)
    
    def center_x(self) -> float:
        return self.x + self.w / 2
    
    def center_y(self) -> float:
        return self.y + self.h / 2
    
    def __repr__(self) -> str:
        return f"UIElement(id={self.id}, name={self.name}, selected={self.selected})"

class DoppioUITestClient:
    def __init__(self, doppio_binary: str, scene_path: str):
        self.doppio_binary = doppio_binary
        self.scene_path = scene_path
        self.process: Optional[subprocess.Popen] = None
        self.request_id = 0
        self.last_response = None
        self.ui_map = None
        self.state = None
    
    def start(self) -> bool:
        try:
            env = {'DOPPIO_TEST_MODE': '1', 'DOPPIO_HEADLESS': '1'}
            cmd = [self.doppio_binary, '--scene', self.scene_path]
            
            logger.info(f"Starting Doppio: {' '.join(cmd)}")
            self.process = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1
            )
            logger.info(f"Process started with PID {self.process.pid}")
            time.sleep(2)
            return True
        except Exception as e:
            logger.error(f"Failed to start Doppio: {e}")
            return False
    
    def send_request(self, request: Dict[str, Any]) -> bool:
        if not self.process or not self.process.stdin:
            logger.error("Process not running")
            return False
        
        self.request_id += 1
        request['id'] = self.request_id
        
        try:
            json_str = json.dumps(request, separators=(',', ':'))
            logger.debug(f"Sending: {json_str}")
            self.process.stdin.write(json_str + '\n')
            self.process.stdin.flush()
            return True
        except Exception as e:
            logger.error(f"Failed to send request: {e}")
            return False
    
    def read_response(self, timeout: float = 5.0) -> bool:
        if not self.process or not self.process.stdout:
            return False
        
        start = time.time()
        while time.time() - start < timeout:
            try:
                line = self.process.stdout.readline()
                if line:
                    if 'REQUEST_RESPONSE:' in line:
                        json_str = line.split('REQUEST_RESPONSE:')[1].strip()
                        self.last_response = json.loads(json_str)
                        logger.debug(f"Response: {self.last_response}")
                        return True
            except:
                pass
            time.sleep(0.01)
        
        logger.error("Timeout waiting for response")
        return False
    
    def get_ui_map(self) -> bool:
        if not self.send_request({"cmd": "get_ui_map"}):
            return False
        
        if not self.read_response():
            return False
        
        if not self.last_response or not self.last_response.get('success'):
            return False
        
        data = self.last_response.get('data', {})
        if isinstance(data, str):
            data = json.loads(data)
        
        self.ui_map = {
            'viewport': data.get('viewport', {}),
            'entities': [DoppioUIElement(e) for e in data.get('entities', [])]
        }
        
        logger.info(f"UI Map: {len(self.ui_map['entities'])} entities")
        for elem in self.ui_map['entities']:
            logger.info(f"  - {elem}")
        
        return True
    
    def get_state(self) -> bool:
        if not self.send_request({"cmd": "get_state"}):
            return False
        
        if not self.read_response():
            return False
        
        if not self.last_response or not self.last_response.get('success'):
            return False
        
        data = self.last_response.get('data', {})
        if isinstance(data, str):
            data = json.loads(data)
        
        self.state = data
        logger.info(f"State: selected={data.get('selected_count')}, entities={data.get('entity_count')}")
        return True
    
    def click(self, x: float, y: float, shift: bool = False, double: bool = False) -> bool:
        request = {
            "cmd": "click",
            "x": x,
            "y": y,
            "shift": shift,
            "double": double
        }
        
        if not self.send_request(request):
            return False
        
        if not self.read_response():
            return False
        
        return self.last_response and self.last_response.get('success', False)
    
    def find_entity(self, name_or_id) -> Optional[DoppioUIElement]:
        if not self.ui_map:
            return None
        
        for elem in self.ui_map['entities']:
            if isinstance(name_or_id, str):
                if elem.name == name_or_id:
                    return elem
            else:
                if elem.id == name_or_id:
                    return elem
        
        return None
    
    def stop(self):
        if self.process:
            try:
                self.process.terminate()
                self.process.wait(timeout=5)
                logger.info("Process terminated")
            except:
                self.process.kill()
                logger.warning("Process force-killed")

def run_test_full_workflow(doppio: str, scene: str) -> bool:
    client = DoppioUITestClient(doppio, scene)
    
    if not client.start():
        return False
    
    try:
        logger.info("\n" + "="*60)
        logger.info("TEST: Full Workflow")
        logger.info("="*60)
        
        logger.info("\n1. Get UI Map")
        if not client.get_ui_map():
            logger.error("✗ Failed to get UI map")
            return False
        assert len(client.ui_map['entities']) == 3, "Should have 3 entities"
        logger.info("✓ Got UI map with 3 entities")
        
        logger.info("\n2. Click Entity_1")
        entity_1 = client.find_entity("Entity_1")
        if not entity_1:
            logger.error("✗ Entity_1 not found")
            return False
        if not client.click(entity_1.center_x(), entity_1.center_y()):
            logger.error("✗ Failed to click Entity_1")
            return False
        if not client.get_state():
            return False
        assert client.state['selected_count'] == 1, "Should have 1 selected"
        logger.info("✓ Clicked Entity_1, 1 entity selected")
        
        logger.info("\n3. Shift+Click Entity_2 (multi-select)")
        entity_2 = client.find_entity("Entity_2")
        if not entity_2:
            logger.error("✗ Entity_2 not found")
            return False
        if not client.click(entity_2.center_x(), entity_2.center_y(), shift=True):
            logger.error("✗ Failed to shift+click Entity_2")
            return False
        if not client.get_state():
            return False
        assert client.state['selected_count'] == 2, "Should have 2 selected"
        logger.info("✓ Shift+clicked Entity_2, 2 entities selected")
        
        logger.info("\n4. Get State")
        if not client.get_state():
            return False
        logger.info(f"✓ Current state: {client.state}")
        
        logger.info("\n5. Double-click Entity_3 to focus camera")
        if not client.get_ui_map():
            return False
        entity_3 = client.find_entity("Entity_3")
        if entity_3:
            if not client.click(entity_3.center_x(), entity_3.center_y(), double=True):
                logger.error("✗ Failed to double-click Entity_3")
                return False
            logger.info("✓ Double-clicked Entity_3, camera focused")
        
        logger.info("\n" + "="*60)
        logger.info("✓ ALL TESTS PASSED")
        logger.info("="*60)
        return True
        
    except AssertionError as e:
        logger.error(f"✗ Assertion failed: {e}")
        return False
    except Exception as e:
        logger.error(f"✗ Test failed: {e}")
        return False
    finally:
        client.stop()

if __name__ == '__main__':
    doppio = sys.argv[1] if len(sys.argv) > 1 else '/home/pedro/repo/caffeine/build/doppio'
    scene = sys.argv[2] if len(sys.argv) > 2 else '/home/pedro/repo/caffeine/tests/fixtures/test_scene_multiobject.caf'
    
    success = run_test_full_workflow(doppio, scene)
    sys.exit(0 if success else 1)
