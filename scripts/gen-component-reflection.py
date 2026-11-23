#!/usr/bin/env python3
"""Generates the reflection entries for the engine's components from their headers.

Reads each component struct (plain data members: f32/bool/ints/Vec2-4/std::string/char[N]/FixedString/
enums, plus nested plain structs which are flattened as "Group: Field") and writes
`src/caffeine/scene/ComponentReflectionGenerated.{includes,entries}.inc`, included by
ComponentReflection.cpp. Component ids are fixed here so they stay stable across regenerations.

    python3 scripts/gen-component-reflection.py          # rewrite the .inc files
    python3 scripts/gen-component-reflection.py --list   # show what was found, write nothing
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src" / "caffeine"
INC = ROOT / "include" / "caffeine"

# (header, qualified type, display name, id)
COMPONENTS = [
    ("ecs/CameraComponents.hpp", "Caffeine::ECS::Camera2DComponent", "Camera 2D", 101),
    ("ecs/CameraComponents.hpp", "Caffeine::ECS::CameraActiveComponent", "Camera Active", 102),
    ("ecs/CameraComponents.hpp", "Caffeine::ECS::Camera3DControllerComponent", "Camera 3D Controller", 103),
    ("ecs/Components.hpp", "Caffeine::ECS::Sprite", "Sprite", 104),
    ("ecs/Components.hpp", "Caffeine::ECS::ParticleEmitterComponent", "Particle Emitter", 105),
    ("ecs/Components.hpp", "Caffeine::ECS::Health", "Health", 106),
    ("ecs/Components.hpp", "Caffeine::ECS::Position2D", "Position 2D", 107),
    ("ecs/Components.hpp", "Caffeine::ECS::Velocity2D", "Velocity 2D", 108),
    ("ecs/Components.hpp", "Caffeine::ECS::Scale2D", "Scale 2D", 109),
    ("ecs/Components.hpp", "Caffeine::ECS::Rotation", "Rotation 2D", 110),
    ("ecs/Components.hpp", "Caffeine::ECS::Acceleration2D", "Acceleration 2D", 111),
    ("ecs/Components.hpp", "Caffeine::ECS::PersistentComponent", "Persistent", 112),
    ("ecs/Components.hpp", "Caffeine::ECS::Tag", "Tag", 113),
    ("ecs/Components.hpp", "Caffeine::ECS::DisabledTag", "Disabled", 114),
    ("ecs/MeshComponents.hpp", "Caffeine::ECS::SkinnedMeshRendererComponent", "Skinned Mesh Renderer", 115),
    ("ecs/SkyboxComponents.hpp", "Caffeine::ECS::SkyboxComponent", "Skybox", 116),
    ("ecs/EnvironmentEffectsComponents.hpp", "Caffeine::ECS::EnvironmentEffectsComponent", "Environment Effects", 117),
    ("ecs/TerrainComponents.hpp", "Caffeine::ECS::TerrainComponent", "Terrain", 118),
    ("caffeine/environment/NaturalFluids.hpp", "Caffeine::ECS::NaturalFluidComponent", "Natural Fluid", 119),
    ("audio/AudioComponents.hpp", "Caffeine::Audio::AudioEmitter", "Audio Emitter", 120),
    ("navigation/NavVolume.hpp", "Caffeine::Navigation::NavAgent", "Nav Agent", 121),
    ("effects/EffectTypes.hpp", "Caffeine::Effects::EffectComponent", "Effect", 122),
    ("ecs/PostProcessComponents.hpp", "Caffeine::ECS::PostProcessComponent", "Post Process", 123),
    ("ecs/ForwardRenderComponents.hpp", "Caffeine::ECS::ForwardRenderFeaturesComponent", "Forward Render Features", 124),
    ("ecs/MapComponents.hpp", "Caffeine::ECS::MapSurface2DComponent", "Map Surface 2D", 125),
    ("ecs/ProceduralComponents.hpp", "Caffeine::ECS::ProceduralWorldComponent", "Procedural World", 126),
    ("ecs/LodComponents.hpp", "Caffeine::ECS::SpriteLod2DComponent", "Sprite LOD 2D", 127),
    ("caffeine/environment/VolumetricClouds.hpp", "Caffeine::ECS::VolumetricCloudsComponent", "Volumetric Clouds", 128),
    ("ecs/WeatherComponents.hpp", "Caffeine::ECS::WeatherComponent", "Weather", 129),
    ("audio/VolumetricComponents.hpp", "Caffeine::Audio::AcousticObstacle", "Acoustic Obstacle", 130),
    ("audio/VolumetricComponents.hpp", "Caffeine::Audio::VolumetricAudioComponent", "Volumetric Audio", 131),
]

HEADERS = sorted({c[0] for c in COMPONENTS} | {
    "ecs/CameraComponents.hpp", "physics/PhysicsComponents2D.hpp"})

# Members never worth exposing: runtime caches and padding.
SKIP = re.compile(r"(revision|dirty|cache|^pad\d*$|carry|^_pad|^_reserved|handle)", re.I)

def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)

def read(header):
    path = INC / header[len("caffeine/"):] if header.startswith("caffeine/") else SRC / header
    return strip_comments(path.read_text())

def find_structs(text):
    """Map struct name -> body text (brace matched), including nested definitions at top level."""
    out = {}
    for m in re.finditer(r"\b(?:struct|class)\s+(\w+)\s*(?::\s*[\w:<>, ]+)?\s*\{", text):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        out[m.group(1)] = text[m.end():i - 1]
    return out

def find_enums(text):
    out = {}
    for m in re.finditer(r"enum\s+class\s+(\w+)\s*(?::\s*\w+)?\s*\{([^}]*)\}", text):
        labels, nxt, dense = {}, 0, True
        for item in [x.strip() for x in m.group(2).split(",") if x.strip()]:
            name, _, value = item.partition("=")
            nxt = int(value.strip(), 0) if value.strip() else nxt
            labels[nxt] = name.strip()
            nxt += 1
        ordered = [labels.get(i) for i in range(max(labels) + 1)] if labels else []
        out[m.group(1)] = ordered if all(ordered) else None
    return out

def top_level_members(body):
    """Drop nested type bodies and function bodies so only data members remain."""
    out, depth, cur = [], 0, ""
    for ch in body:
        if ch == "{":
            if depth == 0 and re.search(r"(struct|class|enum)\b[^;=]*$", cur.strip()) or depth == 0 and re.search(r"\)\s*(const)?\s*$", cur.strip()):
                depth = 1
                cur = ""  # a nested type or function body: skip it
                continue
            if depth == 0:
                cur += ch  # brace initialiser of a member
                depth = -1
                continue
            depth += 1
            continue
        if ch == "}":
            if depth == -1:
                cur += ch
                depth = 0
                continue
            depth -= 1
            continue
        if depth > 0:
            continue
        cur += ch
        if ch == ";" and depth == 0:
            out.append(cur.strip())
            cur = ""
    return out

TYPE_KINDS = {
    "f32": "f", "float": "f", "bool": "b",
    "i32": "i", "int": "i", "int32_t": "i", "u32": "i", "uint32_t": "i", "u16": "i", "u8": "i",
    "i8": "i", "i16": "i", "unsigned": "i", "usize": None,
    "Vec2": "v", "Vec3": "v", "Vec4": "v", "std::string": "s",
}

def humanise(name):
    spaced = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", name)
    spaced = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", " ", spaced)
    return spaced[:1].upper() + spaced[1:].lower().replace(" d ", " D ") if False else spaced[:1].upper() + spaced[1:]

constants = {}

def parse_members(struct, structs, enums, prefix_access, prefix_label, stats, depth=0):
    fields = []
    for decl in top_level_members(structs[struct]):
        decl = re.sub(r"\s+", " ", decl)
        if "static " in decl or "constexpr" in decl or "(" in decl.split("=")[0] or decl.startswith("using") or decl.startswith("friend"):
            continue
        two = re.match(r"^char\s+(\w+)\s*\[\s*(\w+)\s*\]\s*\[", decl)
        if two:
            count = constants.get(two.group(2), int(two.group(2)) if two.group(2).isdigit() else 0)
            for i in range(count):
                fields.append(("s", f"{prefix_label}{humanise(two.group(1))} {i + 1}", f"{prefix_access}.{two.group(1)}[{i}]", None))
            continue
        m = re.match(r"^(?:const\s+)?([\w:<>]+)\s+(\w+)\s*(\[\s*[\w:]+\s*\])?\s*(?:=\s*[^;]*|\{[^;]*\})?;$", decl)
        if not m:
            m2 = re.match(r"^([\w:<>]+)\s+(\w+)\s*(\[\s*[\w:]+\s*\])?\s*;?$", decl)
            if not m2:
                stats["skipped"].append(decl[:60])
                continue
            m = m2
        ctype, name, array = m.group(1), m.group(2), m.group(3)
        ctype = ctype.replace("Caffeine::", "").replace("ECS::", "")
        if SKIP.search(name):
            continue
        access = f"{prefix_access}.{name}"
        label = f"{prefix_label}{humanise(name)}"
        two = re.match(r"^char\s+(\w+)\s*\[\s*(\w+)\s*\]\s*\[", decl)
        if two:
            count = constants.get(two.group(2), int(two.group(2)) if two.group(2).isdigit() else 0)
            for i in range(count):
                fields.append(("s", f"{prefix_label}{humanise(name)} {i + 1}", f"{access}[{i}]", None))
            continue
        if array:
            if ctype == "char":
                fields.append(("s", label, access, None))
            else:
                stats["skipped"].append(f"{ctype} {name}[]")
            continue
        if ctype.startswith("FixedString"):
            fields.append(("s", label, access, None))
            continue
        if ctype in enums:
            if enums[ctype]:
                fields.append(("e", label, access, enums[ctype]))
            else:
                stats["skipped"].append(f"enum {ctype} {name} (not dense)")
            continue
        if ctype in structs and depth < 2 and ctype != struct:
            sub = parse_members(ctype, structs, enums, access, label + ": ", stats, depth + 1)
            fields.extend(sub)
            continue
        kind = TYPE_KINDS.get(ctype)
        if kind:
            fields.append((kind, label, access, None))
        else:
            stats["skipped"].append(f"{ctype} {name}")
    return fields

def main():
    list_only = "--list" in sys.argv
    structs, enums = {}, {}
    for header in HEADERS:
        text = read(header)
        structs.update(find_structs(text))
        enums.update(find_enums(text))
        for m in re.finditer(r"(?:constexpr|const)\s+(?:u32|int|usize|unsigned)\s+(\w+)\s*=\s*(\d+)\s*;", text):
            constants[m.group(1)] = int(m.group(2))
    entries, includes = [], []
    for header in HEADERS:
        includes.append(f'#include "{header}"')
    total = 0
    for header, qualified, display, cid in COMPONENTS:
        short = qualified.split("::")[-1]
        if short not in structs:
            print(f"!! {short} not found in {header}")
            continue
        stats = {"skipped": []}
        fields = parse_members(short, structs, enums, "c", "", stats)
        total += len(fields)
        lines = []
        for kind, label, access, options in fields:
            accessor = f"[](T& c) -> auto& {{ return {access}; }}"
            opts = ""
            if options:
                opts = ", {" + ", ".join(f'"{o}"' for o in options) + "}"
            lines.append(f'            ref<T>("{label}", {accessor}{opts})')
        body = ",\n".join(lines)
        entries.append(f'''        [] {{
            using T = {qualified};
            return component<T>({cid}, "{display}", {{
{body}
            }});
        }}()''')
        print(f"{display:28} id {cid}  {len(fields):3} fields" + (f"   skipped: {stats['skipped'][:4]}" if stats["skipped"] else ""))
    print(f"total {total} fields in {len(COMPONENTS)} components")
    if list_only:
        return
    (SRC / "scene" / "ComponentReflectionGenerated.includes.inc").write_text(
        "// Generated by scripts/gen-component-reflection.py: do not edit.\n" + "\n".join(includes) + "\n")
    (SRC / "scene" / "ComponentReflectionGenerated.entries.inc").write_text(
        "// Generated by scripts/gen-component-reflection.py: do not edit.\n" + ",\n".join(entries) + ",\n")

main()
