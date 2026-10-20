#!/usr/bin/env python3
"""Generates the demo scenes in assets/scenes/. Hand-written scenes live in the
same folder; this script only produces the ones with procedurally placed
geometry (random sphere field, BVH stress test) and the Cornell variants so
that their shared walls stay in one place. Deterministic (fixed seed)."""
import json, random, os, math

OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "scenes")

def save(name, scene):
    with open(os.path.join(OUT, name), "w") as f:
        json.dump(scene, f, indent=2)
        f.write("\n")

def quad(name, corner, u, v, mat):
    return {"type": "quad", "name": name, "corner": corner, "u": u, "v": v, "material": mat}

def cornell(name, desc, objects, extra_materials, spp=256, light=15.0):
    S = 5.55
    mats = {
        "white": {"type": "lambertian", "albedo": [0.73, 0.73, 0.73]},
        "red": {"type": "lambertian", "albedo": [0.65, 0.05, 0.05]},
        "green": {"type": "lambertian", "albedo": [0.12, 0.45, 0.15]},
        "light": {"type": "emissive", "color": [1.0, 0.9, 0.75], "intensity": light},
    }
    mats.update(extra_materials)
    walls = [
        quad("green wall", [0, 0, 0], [0, 0, S], [0, S, 0], "green"),
        quad("red wall", [S, 0, 0], [0, S, 0], [0, 0, S], "red"),
        quad("floor", [0, 0, 0], [S, 0, 0], [0, 0, S], "white"),
        quad("ceiling", [0, S, 0], [0, 0, S], [S, 0, 0], "white"),
        quad("back wall", [0, 0, S], [S, 0, 0], [0, S, 0], "white"),
        quad("ceiling light", [2.13, 5.54, 2.27], [1.30, 0, 0], [0, 0, 1.05], "light"),
    ]
    return {
        "name": name,
        "description": desc,
        "camera": {"look_from": [2.78, 2.78, -8.0], "look_at": [2.78, 2.78, 0], "vfov": 40},
        "render": {"width": 512, "height": 512, "spp": spp, "max_depth": 12, "rr_min_depth": 3,
                   "integrator": "path", "tone_map": "clamp"},
        "background": {"type": "solid", "color": [0, 0, 0]},
        "materials": mats,
        "objects": walls + objects,
    }

def main():
    save("cornell_box.json", cornell(
        "Cornell box",
        "Classic Cornell box: one area light, diffuse walls. Shows indirect illumination and colour bleeding.",
        [
            {"type": "box", "name": "tall block", "min": [2.65, 0, 2.95], "max": [4.30, 3.30, 4.60], "rotate_y": 15, "material": "white"},
            {"type": "box", "name": "short block", "min": [1.30, 0, 0.65], "max": [2.95, 1.65, 2.30], "rotate_y": -18, "material": "white"},
        ], {}))

    save("cornell_glass_metal.json", cornell(
        "Cornell box: glass and metal",
        "Progressive global-illumination showcase: a glass sphere focuses a caustic, a mirror sphere reflects the room.",
        [
            {"type": "sphere", "name": "glass ball", "center": [1.85, 1.0, 1.9], "radius": 1.0, "material": "glass"},
            {"type": "sphere", "name": "mirror ball", "center": [3.75, 1.0, 3.6], "radius": 1.0, "material": "mirror"},
        ],
        {"glass": {"type": "dielectric", "ior": 1.5},
         "mirror": {"type": "metal", "albedo": [0.95, 0.95, 0.95], "roughness": 0.0}},
        spp=1024))

    # Random sphere field (a "complex scene" for the threading benchmark).
    rng = random.Random(7)
    mats = {
        "ground": {"type": "lambertian", "albedo": [0.5, 0.5, 0.5], "albedo2": [0.32, 0.36, 0.3], "checker_scale": 0.5},
        "glass": {"type": "dielectric", "ior": 1.5},
        "big_diffuse": {"type": "lambertian", "albedo": [0.4, 0.2, 0.1]},
        "big_metal": {"type": "metal", "albedo": [0.7, 0.6, 0.5], "roughness": 0.0},
    }
    objs = [{"type": "plane", "name": "ground", "point": [0, 0, 0], "normal": [0, 1, 0], "material": "ground"}]
    k = 0
    for a in range(-11, 11):
        for b in range(-11, 11):
            choose = rng.random()
            c = [a + 0.9 * rng.random(), 0.2, b + 0.9 * rng.random()]
            if math.dist(c, [4, 0.2, 0]) <= 0.9 or math.dist(c, [0, 0.2, 0]) <= 0.9 or math.dist(c, [-4, 0.2, 0]) <= 0.9:
                continue
            name = f"m{k}"; k += 1
            if choose < 0.75:
                mats[name] = {"type": "lambertian", "albedo": [round(rng.random() * rng.random(), 3) for _ in range(3)]}
            elif choose < 0.92:
                mats[name] = {"type": "metal", "albedo": [round(0.5 + 0.5 * rng.random(), 3) for _ in range(3)],
                              "roughness": round(0.4 * rng.random(), 3)}
            else:
                name = "glass"
            objs.append({"type": "sphere", "name": f"sphere {k}", "center": [round(x, 4) for x in c], "radius": 0.2, "material": name})
    objs += [
        {"type": "sphere", "name": "big glass", "center": [0, 1, 0], "radius": 1.0, "material": "glass"},
        {"type": "sphere", "name": "big diffuse", "center": [-4, 1, 0], "radius": 1.0, "material": "big_diffuse"},
        {"type": "sphere", "name": "big metal", "center": [4, 1, 0], "radius": 1.0, "material": "big_metal"},
    ]
    save("sphere_field.json", {
        "name": "Sphere field",
        "description": "~480 spheres with mixed materials under a sky. Used for multithreading benchmarks.",
        "camera": {"look_from": [13, 2, 3], "look_at": [0, 0, 0], "vfov": 20, "aperture": 0.08, "focus_dist": 10},
        "render": {"width": 800, "height": 450, "spp": 128, "max_depth": 12, "integrator": "path", "tone_map": "aces", "exposure": 1.2},
        "background": {"type": "gradient", "top": [0.5, 0.7, 1.0], "bottom": [1, 1, 1]},
        "materials": mats,
        "objects": objs,
    })

    # Triangle-mesh scene: a ring of tori around a finely subdivided icosphere.
    objs = [{"type": "plane", "name": "floor", "point": [0, 0, 0], "normal": [0, 1, 0], "material": "floor"},
            {"type": "mesh", "name": "icosphere", "source": "icosphere", "subdivisions": 6, "smooth": True,
             "scale": [1.2, 1.2, 1.2], "translate": [0, 1.2, 0], "material": "gold"}]
    for i in range(10):
        ang = 2 * math.pi * i / 10
        objs.append({"type": "mesh", "name": f"torus {i}", "source": "torus", "major_radius": 0.45, "minor_radius": 0.15,
                     "segments": 96, "smooth": True, "rotate_y": round(math.degrees(ang), 2),
                     "translate": [round(3.0 * math.cos(ang), 4), 0.15, round(3.0 * math.sin(ang), 4)],
                     "material": "tint" if i % 2 else "glass"})
    save("mesh_bvh.json", {
        "name": "Mesh BVH",
        "description": "~174k triangles (subdivided icosphere + 10 tori). Unrenderable in reasonable time without the BVH.",
        "camera": {"look_from": [0, 4.5, 7.5], "look_at": [0, 0.8, 0], "vfov": 40},
        "render": {"width": 640, "height": 400, "spp": 64, "max_depth": 8, "integrator": "path", "tone_map": "aces"},
        "background": {"type": "gradient", "top": [0.55, 0.65, 0.85], "bottom": [1, 0.97, 0.92], "intensity": 1.0},
        "materials": {
            "floor": {"type": "lambertian", "albedo": [0.75, 0.75, 0.75]},
            "gold": {"type": "metal", "albedo": [0.95, 0.75, 0.35], "roughness": 0.15},
            "tint": {"type": "lambertian", "albedo": [0.2, 0.45, 0.8]},
            "glass": {"type": "dielectric", "ior": 1.5, "tint": [0.9, 1.0, 0.92]},
        },
        "objects": objs,
        "lights": [{"type": "point", "name": "sun-ish", "position": [6, 10, 4], "intensity": [120, 115, 105]}],
    })

if __name__ == "__main__":
    main()
