#!/usr/bin/env python3
import os
import sys
import pytest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from doppio_ui_client import DoppioUITestClient

DOPPIO_BIN = os.environ.get("DOPPIO_BIN", str(Path(__file__).parent.parent / "build" / "doppio"))
VIEWPORT_W = 1280.0
VIEWPORT_H = 720.0

# Entity world positions → screen coords via TestUIMapper formula:
#   screenX = (worldX + 10) * 20,  screenY = (worldY + 10) * 20
ENTITY_1_CENTER = (200.0 + 25.0, 200.0 + 25.0)   # world (0,0) → screen (200,200), center of 50×50 box
ENTITY_2_CENTER = (300.0 + 25.0, 200.0 + 25.0)   # world (5,0) → screen (300,200)
ENTITY_3_CENTER = (400.0 + 25.0, 300.0 + 25.0)   # world (10,5) → screen (400,300)


@pytest.fixture(scope="module")
def client():
    if not Path(DOPPIO_BIN).exists():
        pytest.skip(f"Doppio binary not found: {DOPPIO_BIN}. Build first or set DOPPIO_BIN env.")
    c = DoppioUITestClient(DOPPIO_BIN, "")
    assert c.start(), "Failed to start Doppio in test mode"
    yield c
    c.stop()


class TestUIMap:
    def test_returns_three_entities(self, client):
        assert client.get_ui_map(), "get_ui_map failed"
        assert len(client.ui_map["entities"]) == 3

    def test_entity_names_match(self, client):
        assert client.get_ui_map()
        names = {e.name for e in client.ui_map["entities"]}
        assert names == {"Entity_1", "Entity_2", "Entity_3"}

    def test_viewport_dimensions_reported(self, client):
        assert client.get_ui_map()
        vp = client.ui_map["viewport"]
        assert vp.get("width") == VIEWPORT_W
        assert vp.get("height") == VIEWPORT_H

    def test_entities_have_valid_screen_bounds(self, client):
        assert client.get_ui_map()
        for e in client.ui_map["entities"]:
            assert 0 <= e.x < VIEWPORT_W, f"{e.name} x={e.x} out of viewport"
            assert 0 <= e.y < VIEWPORT_H, f"{e.name} y={e.y} out of viewport"
            assert e.w > 0 and e.h > 0


class TestSelection:
    def test_click_selects_entity(self, client):
        assert client.click(*ENTITY_1_CENTER), "click failed"
        assert client.get_state()
        assert client.state["selected_count"] == 1

    def test_click_outside_deselects(self, client):
        client.click(*ENTITY_1_CENTER)
        assert client.click(10.0, 10.0), "click outside failed"
        assert client.get_state()
        assert client.state["selected_count"] == 0

    def test_click_different_entity_switches_selection(self, client):
        client.click(*ENTITY_1_CENTER)
        assert client.click(*ENTITY_2_CENTER)
        assert client.get_state()
        assert client.state["selected_count"] == 1
        assert client.get_ui_map()
        e2 = client.find_entity("Entity_2")
        assert e2 and e2.selected

    def test_selected_flag_visible_in_ui_map(self, client):
        client.click(*ENTITY_1_CENTER)
        assert client.get_ui_map()
        e1 = client.find_entity("Entity_1")
        assert e1 and e1.selected, "Entity_1 should be selected in ui_map"


class TestMultiSelect:
    def test_shift_click_adds_to_selection(self, client):
        client.click(*ENTITY_1_CENTER)
        assert client.click(*ENTITY_2_CENTER, shift=True)
        assert client.get_state()
        assert client.state["selected_count"] == 2

    def test_shift_click_three_entities(self, client):
        client.click(*ENTITY_1_CENTER)
        client.click(*ENTITY_2_CENTER, shift=True)
        assert client.click(*ENTITY_3_CENTER, shift=True)
        assert client.get_state()
        assert client.state["selected_count"] == 3

    def test_shift_click_toggles_off_already_selected(self, client):
        client.click(*ENTITY_1_CENTER)
        client.click(*ENTITY_2_CENTER, shift=True)
        assert client.click(*ENTITY_1_CENTER, shift=True)
        assert client.get_state()
        assert client.state["selected_count"] == 1

    def test_regular_click_resets_multiselect(self, client):
        client.click(*ENTITY_1_CENTER)
        client.click(*ENTITY_2_CENTER, shift=True)
        client.click(*ENTITY_3_CENTER, shift=True)
        assert client.click(*ENTITY_1_CENTER)
        assert client.get_state()
        assert client.state["selected_count"] == 1


class TestCameraFocus:
    def test_double_click_succeeds(self, client):
        result = client.click(*ENTITY_1_CENTER, double=True)
        assert result, "double-click should succeed on a valid entity"

    def test_double_click_outside_returns_false(self, client):
        result = client.click(10.0, 10.0, double=True)
        assert not result, "double-click on empty space should return false"


class TestStateConsistency:
    def test_entity_count_stays_constant(self, client):
        for _ in range(3):
            assert client.get_state()
            assert client.state["entity_count"] == 3

    def test_ui_map_entity_count_matches_state(self, client):
        assert client.get_ui_map()
        assert client.get_state()
        assert len(client.ui_map["entities"]) == client.state["entity_count"]


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "--tb=short"]))
