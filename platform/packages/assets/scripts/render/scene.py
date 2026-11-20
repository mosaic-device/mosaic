"""Renders the marketing site's device shots in Blender (Cycles).

Run by render.mjs, not directly: it passes a JSON config after `--` with the
model, the screen images, the output folder and the quality. Every shot is a
transparent PNG; the page supplies the background, desk and glows.

Shots
  hero.png                 the device on the desk, calendar on screen, ring blue
  waits-dark.png           S2 before it arrives: calendar, ring dark
  waits-ring.png           S2 as it arrives: ring blue
  waits-review.png         S2 arrived: review request, ring blue
  calm.png                 S3's macro: the knob corner, screen off, warm light
  journey/a-NN.png         S5's reveal: the device lands, server and laptop rise
  journey/a-ring.png       S5 revealed, its ring blue (crossfaded in)
  journey/b-NN.png         S5 to S6: push in, lift, lie down, come apart
  journey.json             frame counts and label anchors for every frame
"""

import json
import math
import os
import sys

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Matrix, Quaternion, Vector

config = json.loads(sys.argv[sys.argv.index("--") + 1])
OUT = config["out"]
QUICK = config.get("quick", False)
# Only the label anchors in journey.json: set everything up, render nothing.
ANCHORS_ONLY = config.get("anchorsOnly", False)
ONLY = set(config.get("only") or [])
os.makedirs(os.path.join(OUT, "journey"), exist_ok=True)

# How far each part lifts when the device comes apart, in layers of 32 mm,
# front of the device on top. Matches the site's labels.
LAYERS = {
    "dial": 5,
    "acrylic": 4,
    "ring-diffuser": 4,
    "display-board": 3,
    "led-ring": 3,
    "carrier-pcb": 2,
    "encoder": 2,
    "light-sensor": 2,
    "case-inner": 1,
    "speaker": 1,
    "usb-c": 1,
    "case-outer": 0,
}
LAYER_GAP = 0.032
LABELLED = ["dial", "acrylic", "display-board", "carrier-pcb", "encoder", "case-outer"]
# S5's world is laid out in the design's drawing units: Mosaic is 150 wide.
UNIT = 0.19 / 150
RING_BLUE = (0.12, 0.39, 1.0, 1.0)
WARM = (1.0, 0.63, 0.31)


def linear(hex_colour):
    """An sRGB hex colour as linear RGBA."""
    out = []
    for i in (1, 3, 5):
        c = int(hex_colour[i : i + 2], 16) / 255
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return (*out, 1.0)


def clamp(v, lo=0.0, hi=1.0):
    return max(lo, min(hi, v))


def smooth(t):
    t = clamp(t)
    return t * t * (3 - 2 * t)


def ease_out(t):
    return 1 - (1 - clamp(t)) ** 4


def span(frame, start, length):
    return clamp((frame - start) / length)


# ------------------------------------------------------------------- scene
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = "CYCLES"
prefs = bpy.context.preferences.addons["cycles"].preferences
for kind in ("METAL", "OPTIX", "CUDA", "HIP", "ONEAPI"):
    try:
        prefs.compute_device_type = kind
        prefs.get_devices()
        if any(d.type != "CPU" for d in prefs.devices):
            for d in prefs.devices:
                d.use = True
            scene.cycles.device = "GPU"
            break
    except TypeError:
        continue
scene.cycles.samples = 12 if QUICK else 64
scene.cycles.use_denoising = True
scene.render.film_transparent = True
scene.render.resolution_percentage = 50 if QUICK else 100
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.view_settings.view_transform = "AgX"
scene.view_settings.look = "None"

world = bpy.data.worlds.new("world")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = linear("#16181a")
world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
scene.world = world


def add(obj):
    scene.collection.objects.link(obj)
    return obj


def empty(name, location=(0, 0, 0)):
    obj = add(bpy.data.objects.new(name, None))
    obj.location = location
    return obj


def material(name, colour, roughness=0.85, metallic=0.0, emission=None, strength=0.0, specular=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = linear(colour)
    p.inputs["Roughness"].default_value = roughness
    p.inputs["Metallic"].default_value = metallic
    p.inputs["Specular IOR Level"].default_value = specular
    if emission:
        p.inputs["Emission Color"].default_value = linear(emission)
        p.inputs["Emission Strength"].default_value = strength
    return m


def box(name, size, location, mat, parent=None):
    """A box `size` (x, y, z) in metres, centred on `location`."""
    mesh = bpy.data.meshes.new(name)
    sx, sy, sz = (s / 2 for s in size)
    verts = [(x, y, z) for x in (-sx, sx) for y in (-sy, sy) for z in (-sz, sz)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    mesh.from_pydata(verts, [], faces)
    mesh.materials.append(mat)
    obj = add(bpy.data.objects.new(name, mesh))
    obj.location = location
    if parent:
        obj.parent = parent
    return obj


def at(a, h, b):
    """A point on S5's plate, from design units (A, height, B) to metres."""
    return Vector((a * UNIT, -b * UNIT, h * UNIT))


# Fading: each material's shader is mixed with transparency by a value node.
faders = {}


def make_fadable(mat, group):
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    out = next(n for n in nodes if n.type == "OUTPUT_MATERIAL")
    if not out.inputs["Surface"].links:
        return
    shader = out.inputs["Surface"].links[0].from_socket
    mix = nodes.new("ShaderNodeMixShader")
    clear = nodes.new("ShaderNodeBsdfTransparent")
    value = nodes.new("ShaderNodeValue")
    value.outputs[0].default_value = 1.0
    links.new(value.outputs[0], mix.inputs[0])
    links.new(clear.outputs[0], mix.inputs[1])
    links.new(shader, mix.inputs[2])
    links.new(mix.outputs[0], out.inputs["Surface"])
    faders.setdefault(group, []).append(value)


def set_fade(group, value):
    for node in faders.get(group, []):
        node.outputs[0].default_value = value


def materials_of(objs):
    seen = []
    for obj in objs:
        for slot in getattr(obj, "material_slots", []):
            if slot.material and slot.material not in seen:
                seen.append(slot.material)
    return seen


def descendants(obj):
    out = [obj]
    for child in obj.children:
        out += descendants(child)
    return out


def hide(obj, hidden):
    for o in descendants(obj):
        o.hide_render = hidden


# ------------------------------------------------------------------ device
bpy.ops.import_scene.gltf(filepath=config["model"])
tops = [o for o in bpy.context.selected_objects if o.parent is None]
parts = {o.name.split(".")[0]: o for o in tops}

# The model's normals smooth across flat faces, which shades them in
# triangles (the acrylic, the case around the knob). Drop them and smooth
# only across gentle angles: flat faces render flat, curves stay round.
for top in tops:
    for o in descendants(top):
        if o.type != "MESH":
            continue
        with bpy.context.temp_override(object=o, active_object=o, selected_objects=[o], selected_editable_objects=[o]):
            if o.data.has_custom_normals:
                bpy.ops.mesh.customdata_custom_splitnormals_clear()
        o.data.set_sharp_from_angle(angle=math.radians(30))

# Pivot at the device's centre so it can turn in place.
lo = Vector((1e9, 1e9, 1e9))
hi = Vector((-1e9, -1e9, -1e9))
bpy.context.view_layer.update()
for top in tops:
    for o in descendants(top):
        if o.type != "MESH":
            continue
        for corner in o.bound_box:
            w = o.matrix_world @ Vector(corner)
            lo = Vector(map(min, lo, w))
            hi = Vector(map(max, hi, w))
centre = (lo + hi) / 2
device = empty("device", centre)
for top in tops:
    top.parent = device
    top.location -= centre
home = {name: obj.location.copy() for name, obj in parts.items()}
owner = {o.name: name for name, top in parts.items() for o in descendants(top)}

# Materials: the page's look. The case colour in the model is a placeholder.
screen_material = ring_material = None
for mat in materials_of([o for t in tops for o in descendants(t)]):
    nodes = mat.node_tree.nodes
    p = next((n for n in nodes if n.type == "BSDF_PRINCIPLED"), None)
    if not p:
        continue
    if mat.name.startswith("case"):
        p.inputs["Base Color"].default_value = linear("#2a2c2e")
        p.inputs["Roughness"].default_value = 0.62
    elif mat.name.startswith("aluminum"):
        p.inputs["Base Color"].default_value = linear("#4a4d50")
        p.inputs["Metallic"].default_value = 1.0
        p.inputs["Roughness"].default_value = 0.34
    elif mat.name.startswith("diffuser"):
        p.inputs["Base Color"].default_value = linear("#2b2d30")
        p.inputs["Roughness"].default_value = 0.55
        p.inputs["Emission Color"].default_value = RING_BLUE
        ring_material = p
    elif mat.name.startswith("screen"):
        p.inputs["Base Color"].default_value = (0, 0, 0, 1)
        p.inputs["Roughness"].default_value = 0.18
        image = nodes.new("ShaderNodeTexImage")
        mat.node_tree.links.new(image.outputs[0], p.inputs["Emission Color"])
        screen_material = (p, image)
    make_fadable(mat, "device")

screens = {k: bpy.data.images.load(v) for k, v in config["screens"].items()}


def set_screen(variant, lit=1.0):
    p, image = screen_material
    image.image = screens[variant]
    p.inputs["Emission Strength"].default_value = lit * 1.15


def set_ring(on):
    ring_material.inputs["Emission Strength"].default_value = on * 3.0


# The device's front: the screen's normal, in the device's own frame.
display = parts["display-board"]
screen_slot = next(i for i, s in enumerate(display.material_slots) if s.material and s.material.name.startswith("screen"))
normal = Vector()
for poly in display.data.polygons:
    if poly.material_index == screen_slot:
        normal += poly.normal * poly.area
front = (display.matrix_world.to_3x3() @ normal).normalized()
# Turned so the screen faces straight up, for S6.
flat = front.rotation_difference(Vector((0, 0, 1)))
# The acrylic sits exactly on the case's front, and two surfaces in one plane
# render in flickering triangles. Lift it off by a fraction of a millimetre.
home["acrylic"] = home["acrylic"] + front * 0.0004


def explode(e):
    for name, obj in parts.items():
        obj.location = home[name] + front * (LAYERS.get(name, 0) * LAYER_GAP * e)


# ------------------------------------------------------------ S5's world
diorama = empty("diorama")
matte = {c: material(f"matte{c}", c) for c in ("#18191B", "#303335", "#111213", "#222426")}
# The plate barely reflects, so it stays darker than everything on it.
plate_material = material("plate", "#070808", roughness=1.0, specular=0.25)
box("plate", (420 * UNIT, 320 * UNIT, 12 * UNIT), at(210, -6, 160), plate_material, diorama)

server = empty("server")
server.parent = diorama
box("server-body", (110 * UNIT, 110 * UNIT, 64 * UNIT), at(95, 32, 115), matte["#18191B"], server)
for b in (80, 100, 120):
    box("vent", (70 * UNIT, 3 * UNIT, 1.5 * UNIT), at(95, 64.6, b), matte["#303335"], server)

laptop = empty("laptop")
laptop.parent = diorama
box("laptop-base", (110 * UNIT, 60 * UNIT, 4 * UNIT), at(95, 2, 260), matte["#18191B"], laptop)
lid = empty("lid", at(95, 4, 231))
lid.parent = laptop
lid.rotation_euler = (-0.18, 0, 0)
box("lid", (110 * UNIT, 3 * UNIT, 58 * UNIT), (0, 0, 29 * UNIT), matte["#111213"], lid)
glow = material("laptop-screen", "#070808", roughness=0.3, emission="#1a1b1d", strength=1.0)
box("laptop-screen", (96 * UNIT, 0.5 * UNIT, 46 * UNIT), (0, -1.8 * UNIT, 30 * UNIT), glow, lid)

accounts = []
for i, (a, b) in enumerate([(-65, 125), (-35, 125), (-50, 155), (-20, 155)]):
    tile = box(f"account{i}", (22 * UNIT, 22 * UNIT, 6 * UNIT), at(a, 120, b), material(f"account{i}", "#222426"))
    tile.parent = diorama
    make_fadable(tile.material_slots[0].material, f"account{i}")
    accounts.append((tile, tile.location.copy()))

# Square pipes joining everything up: your accounts into your server, your
# server into Mosaic, and out of Mosaic's far side into the back of your
# laptop. On the page, light runs
# along them in that order. Routes are (A, height, B) corners, through the
# pipe's middle.
PIPES = {
    "accounts": [(-42, 117, 140), (-42, 40, 140), (40, 40, 140)],
    "mosaic": [(150, 5, 165), (230, 5, 165)],
    "laptop": [(230, 5, 200), (100, 5, 200), (100, 5, 224)],
}
PIPE = 10  # across, each way
pipe_material = material("pipe", "#1A1C1E", specular=0.4)
for name, route in PIPES.items():
    for i, (p0, p1) in enumerate(zip(route, route[1:])):
        # Each length runs half a pipe past its far end, filling the corner
        # block (or burying its end in what it joins). Only the first also
        # runs half a pipe back past its start; the rest start where the
        # corner block ends, so no two lengths share a face.
        axis = next(k for k in range(3) if p0[k] != p1[k])
        sign = 1 if p1[axis] > p0[axis] else -1
        run_start = list(p0)
        run_end = list(p1)
        run_start[axis] -= sign * PIPE / 2 * (1 if i == 0 else -1)
        run_end[axis] += sign * PIPE / 2
        run_lo = [min(u, v) for u, v in zip(run_start, run_end)]
        run_hi = [max(u, v) for u, v in zip(run_start, run_end)]
        size = [(h - l) if k == axis else PIPE for k, (l, h) in enumerate(zip(run_lo, run_hi))]
        middle = [(u + v) / 2 for u, v in zip(run_lo, run_hi)]
        box(f"pipe-{name}-{i}", (size[0] * UNIT, size[2] * UNIT, size[1] * UNIT), at(*middle), pipe_material, diorama)
make_fadable(pipe_material, "pipes")
for mat in materials_of(descendants(diorama)):
    if not mat.name.startswith(("account", "pipe")):
        make_fadable(mat, "diorama")
MOSAIC_ON_PLATE = at(305, 0, 180)

shadow = box("shadow", (4, 4, 0.001), (0, 0, 0), material("shadow", "#000000"))
shadow.is_shadow_catcher = True


def faded_desk(half_width, half_depth):
    """A dark desk under the device that fades out all round, so its render
    has no edges: a patch of surface, the page's background beyond it."""
    m = bpy.data.materials.new("desk")
    m.use_nodes = True
    nodes, links = m.node_tree.nodes, m.node_tree.links
    surface = nodes["Principled BSDF"]
    surface.inputs["Base Color"].default_value = linear("#121314")
    surface.inputs["Roughness"].default_value = 0.55
    coord = nodes.new("ShaderNodeTexCoord")
    scale = nodes.new("ShaderNodeMapping")
    scale.inputs["Scale"].default_value = (1 / half_width, 1 / half_depth, 1)
    falloff = nodes.new("ShaderNodeTexGradient")
    falloff.gradient_type = "SPHERICAL"
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.interpolation = "EASE"
    ramp.color_ramp.elements[1].position = 0.65
    clear = nodes.new("ShaderNodeBsdfTransparent")
    mix = nodes.new("ShaderNodeMixShader")
    links.new(coord.outputs["Object"], scale.inputs["Vector"])
    links.new(scale.outputs[0], falloff.inputs[0])
    links.new(falloff.outputs["Fac"], ramp.inputs[0])
    links.new(ramp.outputs[0], mix.inputs[0])
    links.new(clear.outputs[0], mix.inputs[1])
    links.new(surface.outputs[0], mix.inputs[2])
    links.new(mix.outputs[0], nodes["Material Output"].inputs["Surface"])
    desk = box("desk", (2 * half_width, 2 * half_depth, 0.001), (0, 0.02, -0.0005), m)
    desk.hide_render = True
    return desk


desk = faded_desk(0.17, 0.11)

# ------------------------------------------------------------------ lights
def area(name, energy, size, colour=(1, 1, 1)):
    data = bpy.data.lights.new(name, "AREA")
    data.energy = energy
    data.size = size
    data.color = colour
    obj = add(bpy.data.objects.new(name, data))
    aim = obj.constraints.new("TRACK_TO")
    aim.track_axis = "TRACK_NEGATIVE_Z"
    aim.up_axis = "UP_Y"
    return obj


key = area("key", 60, 0.9)
fill = area("fill", 12, 1.2)
rim = area("rim", 40, 0.5, WARM)
# The camera and the lights aim at separate points: moving the lights with
# the device must never turn the camera.
target = empty("target")
light_target = empty("light-target")
for light in (key, fill, rim):
    light.constraints[0].target = light_target

camera_data = bpy.data.cameras.new("camera")
camera_data.sensor_width = 36
camera = add(bpy.data.objects.new("camera", camera_data))
scene.camera = camera
aim = camera.constraints.new("TRACK_TO")
aim.target = target
aim.track_axis = "TRACK_NEGATIVE_Z"
aim.up_axis = "UP_Y"


def direction(azimuth, elevation):
    """From the subject towards the viewer: the device's front is -Y."""
    az, el = math.radians(azimuth), math.radians(elevation)
    return Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))


def light_around(point, scale, warm=0.0, key_energy=60):
    """A product setup around `point`, sized to the shot."""
    light_target.location = point
    key.location = point + Vector((-1.1, -1.2, 1.4)) * scale
    fill.location = point + Vector((1.4, -1.0, 0.2)) * scale
    rim.location = point + Vector((0.9, 1.3, 0.7)) * scale
    key.data.energy = key_energy * scale**2
    fill.data.energy = 12 * scale**2
    rim.data.energy = (40 + 220 * warm) * scale**2
    rim.data.color = WARM if warm else (1, 1, 1)


def project(points):
    """Points as fractions of the frame (x right, y up), lens shift included."""
    bpy.context.view_layer.update()
    return [world_to_camera_view(scene, camera, p) for p in points]


def device_corners(names=None):
    bpy.context.view_layer.update()
    out = []
    for name, obj in parts.items():
        if names and name not in names:
            continue
        for o in descendants(obj):
            if o.type == "MESH":
                out += [o.matrix_world @ Vector(c) for c in o.bound_box]
    return out


def frame_perspective(azimuth, elevation, focus, fill_width, at_x=0.5, at_y=0.5, names=None):
    """Points the camera at `focus` and sets its distance so the device (or
    `names`) spans `fill_width` of the frame, centred on (at_x, at_y)."""
    camera_data.type = "PERSP"
    camera_data.lens = 100
    camera_data.shift_x = camera_data.shift_y = 0
    target.location = focus
    d = 1.0
    for _ in range(4):
        camera.location = focus + direction(azimuth, elevation) * d
        xs = [p.x for p in project(device_corners(names))]
        d *= (max(xs) - min(xs)) / fill_width
    camera.location = focus + direction(azimuth, elevation) * d
    for _ in range(3):
        view = project(device_corners(names))
        cx = (min(p.x for p in view) + max(p.x for p in view)) / 2
        cy = (min(p.y for p in view) + max(p.y for p in view)) / 2
        aspect = scene.render.resolution_y / scene.render.resolution_x
        camera_data.shift_x += cx - at_x
        camera_data.shift_y += (cy - (1 - at_y)) * aspect
    return d


def render(path):
    if ANCHORS_ONLY:
        return
    scene.render.filepath = os.path.join(OUT, path)
    bpy.ops.render.render(write_still=True)


def resolution(w, h):
    scene.render.resolution_x, scene.render.resolution_y = w, h


def wanted(name):
    return not ONLY or name in ONLY


# --------------------------------------------------------- the device shots
def stand(on_desk=True):
    """The device upright at the origin, base on the desk."""
    device.location = Vector((0, 0, centre.z - lo.z))
    device.rotation_mode = "QUATERNION"
    device.rotation_quaternion = Quaternion()
    explode(0)
    set_fade("device", 1)
    hide(diorama, True)
    shadow.hide_render = not on_desk


def mid():
    bpy.context.view_layer.update()
    return device.matrix_world.translation.copy()


# The screen leans back with the case; the camera looks at it square on.
LEAN = math.degrees(front.angle(Vector((0, -1, 0)))) if front.z > 0 else 0

if wanted("hero"):
    # Square on and a little above, on a rendered desk that fades out before
    # the frame's edges, warm light from behind falling on it. The frame is
    # wide so the fade fits; the page scales it to keep the device's size.
    stand(on_desk=False)
    desk.hide_render = False
    resolution(2400, 1200)
    set_screen("calendar")
    set_ring(1)
    d = frame_perspective(0, LEAN + 12, mid(), 0.45)
    light_around(mid(), d * 0.9, warm=0.6)
    render("hero.png")
    desk.hide_render = True

if wanted("waits"):
    # Straight on, square to the screen; floating, with no desk under it.
    stand(on_desk=False)
    resolution(1718, 1074)
    d = frame_perspective(0, LEAN, mid(), 0.94)
    light_around(mid(), d * 0.9)
    for name, variant, ring in (("waits-dark", "calendar", 0), ("waits-ring", "calendar", 1), ("waits-review", "review", 1)):
        set_screen(variant)
        set_ring(ring)
        render(f"{name}.png")

if wanted("calm"):
    stand()
    resolution(1920, 880)
    set_screen("calendar", lit=0)
    set_ring(0)
    dial_centre = sum(device_corners(["dial"]), Vector()) / 8
    d = frame_perspective(34, LEAN + 10, dial_centre, 0.21, at_x=0.58, names=["dial"])
    light_around(dial_centre, d * 0.5, warm=1.0, key_energy=18)
    render("calm.png")

# ------------------------------------------------------------- the journey
# One fixed isometric camera; S5's framing pushes in to S6's.
ISO = Vector((1, -1, 1)).normalized()


def frame_ortho(points, width_share, at_x, at_y, by="width"):
    """Ortho scale and shift so `points` span `width_share` of the frame's
    width (or height, `by`), centred on (at_x, at_y)."""
    camera_data.type = "ORTHO"
    camera_data.shift_x = camera_data.shift_y = 0
    centre_ = sum(points, Vector()) / len(points)
    target.location = centre_
    camera.location = centre_ + ISO * 5
    camera_data.ortho_scale = 1.0
    aspect = scene.render.resolution_y / scene.render.resolution_x
    view = project(points)
    w = max(p.x for p in view) - min(p.x for p in view)
    h = max(p.y for p in view) - min(p.y for p in view)
    scale = w / width_share if by == "width" else h / width_share
    camera_data.ortho_scale = scale
    view = project(points)
    cx = (min(p.x for p in view) + max(p.x for p in view)) / 2
    cy = (min(p.y for p in view) + max(p.y for p in view)) / 2
    return {
        "target": centre_.copy(),
        "scale": scale,
        "shift": Vector(((cx - at_x), (cy - (1 - at_y)) * aspect)),
    }


def plate_points():
    return [at(a, 0, b) for a in (0, 420) for b in (0, 320)]


# The page shows these 16:9 frames cropped to its 16:10 stage, so place
# subjects by where they land on the stage, not on the frame.
STAGE_SHARE = (16 / 10) / (16 / 9)


def stage_x(x):
    return 0.5 + (x - 0.5) * STAGE_SHARE


def set_camera(view):
    target.location = view["target"]
    camera.location = view["target"] + ISO * 5
    camera_data.ortho_scale = view["scale"]
    camera_data.shift_x, camera_data.shift_y = view["shift"]


def place_device(lift, turn, e):
    """On the plate at Mosaic's spot, lifted, turned towards flat, opened."""
    base = MOSAIC_ON_PLATE + Vector((0, 0, centre.z - lo.z + lift))
    device.location = base
    device.rotation_mode = "QUATERNION"
    device.rotation_quaternion = Quaternion().slerp(flat, turn)
    explode(e)


def anchors():
    out = {}
    # Label points: above the server's top corner, and on the plate's front
    # edges below the laptop and Mosaic, so labels sit off the plate.
    named = {
        "serverTop": at(40, 64, 60),
        "accounts": at(-42, 135, 140),
        "laptopBelow": at(110, -12, 320),
        "mosaicBelow": at(420, -12, 180),
    }
    for name, p in named.items():
        v = project([p])[0]
        out[name] = [round(v.x, 4), round(1 - v.y, 4)]
    for name, (obj, co) in label_points.items():
        v = project([obj.matrix_world @ co])[0]
        out[name] = [round(v.x, 4), round(1 - v.y, 4)]
    return out


# Where S6's labels start on the page (stage x 1190 of 1440), as a fraction
# of the frame.
LABEL_X = stage_x(1190 / 1440)
# A label's height and gap, as a fraction of the frame's height, so each
# label sits level with its point and its leader runs flat.
LABEL_GAP = 53 / 900
label_points = {}


# Parts you see through: they hide nothing behind them.
CLEAR = {"acrylic"}
# The case is the backdrop the other parts float over; a leader may lie on
# it, but not across another part.
BACKDROP = {"case-outer", "case-inner"}


def part_at(x, y, looking_for):
    """The device part seen at frame point (x, y), or None. Looks through
    clear parts unless it is looking for one."""
    tr, br, bl, tl = camera_data.view_frame(scene=scene)
    local = bl + (br - bl) * x + (tl - bl) * y
    forward = (camera.matrix_world.to_3x3() @ Vector((0, 0, -1))).normalized()
    origin = camera.matrix_world @ local - forward * 10
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for _ in range(8):
        hit, location, _, _, obj, _ = scene.ray_cast(depsgraph, origin, forward)
        if not hit:
            return None
        name = owner.get(obj.original.name)
        if name not in CLEAR or name == looking_for:
            return name
        origin = location + forward * 1e-4
    return None


def clear_points(name):
    """Points on a part the camera sees, each with a clear run right to the
    label column (over nothing but the case): the right-most in each thin
    band of the frame's height, right-most first. As (down, x, object, co),
    `down` measured from the frame's top."""
    candidates = []
    for o in descendants(parts[name]):
        if o.type != "MESH":
            continue
        verts = list(o.data.vertices)
        for v in verts[:: max(1, len(verts) // 400)]:
            p = project([o.matrix_world @ v.co])[0]
            candidates.append((p.x, p.y, o, v.co.copy()))
    candidates.sort(key=lambda c: -c[0])
    best = {}
    for x, y, o, co in candidates:
        band = round((1 - y) * 225)
        if band in best:
            continue
        # Just inside the point, it must be this part; from there to the
        # column, nothing but this part, the case or empty space.
        if part_at(x - 0.003, y, name) != name:
            continue
        steps = 24
        run = [part_at(x + (LABEL_X - x) * i / steps, y, name) for i in range(1, steps)]
        if all(r in (None, name) or r in BACKDROP for r in run):
            best[band] = (1 - y, x, o, co)
    return sorted(best.values(), key=lambda c: -c[1])


def choose_label_points():
    """A point on each labelled part, each a label's height from the others,
    so every label sits level with its point and its leader runs flat to it
    without crossing another part, the leaders as short as they can be.
    Picked once, in S6's final pose, then followed through every frame."""
    others = descendants(diorama) + [shadow]
    for o in others:
        o.hide_viewport = True
    bpy.context.view_layer.update()
    options = {name: clear_points(name) for name in LABELLED}
    for o in others:
        o.hide_viewport = False
    bpy.context.view_layer.update()

    # The set with the shortest leaders in all, by branch and bound.
    # Squared, so one long leader costs more than a few middling ones.
    shortest = {name: (LABEL_X - opts[0][1]) ** 2 for name, opts in options.items() if opts}
    best = {"cost": math.inf, "chosen": None}

    def search(i, chosen, cost):
        if cost + sum(shortest[n] for n in LABELLED[i:]) >= best["cost"]:
            return
        if i == len(LABELLED):
            best["cost"], best["chosen"] = cost, chosen
            return
        name = LABELLED[i]
        for option in options[name]:
            if all(abs(option[0] - c[0]) >= LABEL_GAP for c in chosen.values()):
                search(i + 1, {**chosen, name: option}, cost + (LABEL_X - option[1]) ** 2)

    search(0, {}, 0.0)
    chosen = best["chosen"]
    if not chosen:
        raise RuntimeError("No way to label every part without crossings")
    for name, (_, _, o, co) in chosen.items():
        label_points[name] = (o, co)


def flat_colour(name, colour):
    """A material that is only light of one colour: no shading at all."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nodes = m.node_tree.nodes
    nodes.clear()
    emit = nodes.new("ShaderNodeEmission")
    emit.inputs["Color"].default_value = linear(colour)
    out = nodes.new("ShaderNodeOutputMaterial")
    m.node_tree.links.new(emit.outputs[0], out.inputs["Surface"])
    return m


def render_pipe_mask(path):
    """Where the pipes show in S5's settled frame: the pipes white, everything
    else black, so the page can keep its light inside them."""
    white, black = flat_colour("mask-white", "#FFFFFF"), flat_colour("mask-black", "#000000")
    saved = {}
    for o in scene.objects:
        if o.type != "MESH":
            continue
        saved[o] = [slot.material for slot in o.material_slots]
        for slot in o.material_slots:
            slot.material = white if o.name.startswith("pipe-") else black
    view = scene.view_settings.view_transform
    scene.view_settings.view_transform = "Standard"
    render(path)
    scene.view_settings.view_transform = view
    for o, materials in saved.items():
        for slot, m in zip(o.material_slots, materials):
            slot.material = m


def pipe_paths():
    """Each pipe's route through its middle as frame points, every 2 design
    units, for the light the page runs along them (the mask hides it where
    something stands in front)."""
    out = {}
    for name, route in PIPES.items():
        points = []
        for p0, p1 in zip(route, route[1:]):
            length = max(abs(u - v) for u, v in zip(p0, p1))
            steps = max(1, round(length / 2))
            for i in range(steps + (1 if p1 == route[-1] else 0)):
                t = i / steps
                v = project([at(*(u + (w - u) * t for u, w in zip(p0, p1)))])[0]
                points.append([round(v.x, 4), round(1 - v.y, 4)])
        out[name] = points
    return out


A_FRAMES, B_FRAMES, B_MOVE = 21, 90, 60
journey = {"size": [1600, 900], "a": A_FRAMES, "b": B_FRAMES, "bMove": B_MOVE, "anchors": {"a": [], "b": []}}

if wanted("journey"):
    resolution(1600, 900)
    hide(diorama, False)
    shadow.hide_render = True
    set_screen("review")
    # S6's framing: the device fully apart and flat, centred on its slot.
    place_device(0.06, 1, 1)
    # Right of centre: the copy sits left, the labels in a column right.
    end = frame_ortho(device_corners(), 0.84, stage_x(0.62), 0.5, by="height")
    set_camera(end)
    choose_label_points()
    # S5's framing: the plate where the design drew it.
    place_device(0, 0, 0)
    start = frame_ortho(plate_points(), 0.4, stage_x(0.64), 0.64)
    light_around(mid(), 0.7)

    # S5's reveal, played on a timer: lands, rises, settles.
    set_camera(start)
    set_ring(0)
    for f in range(A_FRAMES):
        land = ease_out(span(f, 0, 14))
        place_device(0.12 * (1 - land), 0, 0)
        set_fade("device", ease_out(span(f, 0, 7)))
        set_fade("diorama", 1)
        server.location.z = -70 * UNIT * (1 - ease_out(span(f, 4, 12)))
        laptop.location.z = -70 * UNIT * (1 - ease_out(span(f, 6, 12)))
        for i, (tile, rest) in enumerate(accounts):
            t = ease_out(span(f, 7 + i, 12))
            tile.location = rest + Vector((0, 0, 20 * UNIT * (1 - t)))
            set_fade(f"account{i}", t)
        set_fade("pipes", ease_out(span(f, 13, 7)))
        journey["anchors"]["a"].append(anchors())
        render(f"journey/a-{f:02d}.png")
    set_ring(1)
    render("journey/a-ring.png")
    render_pipe_mask("journey/pipes.png")
    journey["pipes"] = pipe_paths()

    # S5 to S6: scroll-driven. The move takes B_MOVE frames and opens the
    # device a third of the way; the rest is the final pull, in place.
    for f in range(B_FRAMES):
        move = smooth(f / B_MOVE)
        settle = smooth((f - B_MOVE) / (B_FRAMES - 1 - B_MOVE))
        view = {
            "target": start["target"].lerp(end["target"], move),
            "scale": math.exp(math.log(start["scale"]) * (1 - move) + math.log(end["scale"]) * move),
            "shift": start["shift"].lerp(end["shift"], move),
        }
        set_camera(view)
        # The lights stay with the device, so its lighting holds steady while
        # the camera pushes in.
        light_around(mid(), 0.7)
        fade = 1 - smooth(span(f, 4, 30))
        set_fade("diorama", fade)
        for i in range(len(accounts)):
            set_fade(f"account{i}", fade)
        set_fade("pipes", fade)
        hide(diorama, fade <= 0.001)
        e = 0.35 * smooth(span(f, 15, B_MOVE - 15)) + 0.65 * settle
        place_device(0.06 * smooth(span(f, 4, 20)), smooth(span(f, 10, 40)), e)
        journey["anchors"]["b"].append(anchors())
        render(f"journey/b-{f:02d}.png")

    with open(os.path.join(OUT, "journey.json"), "w") as fh:
        json.dump(journey, fh)
