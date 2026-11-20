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
LABELLED = ["dial", "acrylic", "display-board", "carrier-pcb", "case-outer"]
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


def material(name, colour, roughness=0.85, metallic=0.0, emission=None, strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = linear(colour)
    p.inputs["Roughness"].default_value = roughness
    p.inputs["Metallic"].default_value = metallic
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


def explode(e):
    for name, obj in parts.items():
        obj.location = home[name] + front * (LAYERS.get(name, 0) * LAYER_GAP * e)


# ------------------------------------------------------------ S5's world
diorama = empty("diorama")
matte = {c: material(f"matte{c}", c) for c in ("#0D0E0E", "#18191B", "#303335", "#111213", "#222426")}
box("plate", (420 * UNIT, 320 * UNIT, 12 * UNIT), at(210, -6, 160), matte["#0D0E0E"], diorama)
dash = material("dash", "#7540B5", emission="#7540B5", strength=1.2)
for (a0, b0), (a1, b1) in [((0, 0), (420, 0)), ((420, 0), (420, 320)), ((420, 320), (0, 320)), ((0, 320), (0, 0))]:
    length = math.hypot(a1 - a0, b1 - b0)
    for s in range(0, int(length), 11):
        t = (s + 2.5) / length
        a, b = a0 + (a1 - a0) * t, b0 + (b1 - b0) * t
        size = (5 * UNIT, 1.2 * UNIT, 0.6 * UNIT) if b0 == b1 else (1.2 * UNIT, 5 * UNIT, 0.6 * UNIT)
        box("dash", size, at(a, 0.4, b), dash, diorama)

server = empty("server")
server.parent = diorama
box("server-body", (110 * UNIT, 110 * UNIT, 64 * UNIT), at(95, 32, 115), matte["#18191B"], server)
for b in (80, 100, 120):
    box("vent", (70 * UNIT, 3 * UNIT, 1.5 * UNIT), at(95, 64.6, b), matte["#303335"], server)
status = material("status", "#92979B", emission="#92979B", strength=0.6)
box("status", (5 * UNIT, 1 * UNIT, 5 * UNIT), at(140, 56, 170.5), status, server)

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
for mat in materials_of(descendants(diorama)):
    if not mat.name.startswith("account"):
        make_fadable(mat, "diorama")
MOSAIC_ON_PLATE = at(305, 0, 180)

shadow = box("shadow", (4, 4, 0.001), (0, 0, 0), material("shadow", "#000000"))
shadow.is_shadow_catcher = True

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
    """Points as fractions of the frame (x right, y up). Blender's
    world_to_camera_view ignores lens shift, so apply it here."""
    bpy.context.view_layer.update()
    aspect = scene.render.resolution_y / scene.render.resolution_x
    sx, sy = camera_data.shift_x, camera_data.shift_y / aspect
    out = []
    for p in points:
        v = world_to_camera_view(scene, camera, p)
        out.append(Vector((v.x - sx, v.y - sy, v.z)))
    return out


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
    stand()
    resolution(1448, 905)
    set_screen("calendar")
    set_ring(1)
    d = frame_perspective(14, LEAN + 6, mid(), 0.94)
    light_around(mid(), d * 0.9)
    render("hero.png")

if wanted("waits"):
    stand()
    resolution(1718, 1074)
    d = frame_perspective(24, LEAN + 6, mid(), 0.94)
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
    named = {
        "server": at(150, 64, 60),
        "serverFront": at(95, 64, 170),
        "laptop": at(95, 0, 290),
        "accounts": at(-42, 135, 140),
        "yours": at(0, 0, 0),
    }
    for name, p in named.items():
        v = project([p])[0]
        out[name] = [round(v.x, 4), round(1 - v.y, 4)]
    corners = device_corners()
    top = max(corners, key=lambda c: c.z)
    v = project([top])[0]
    out["mosaicTop"] = [round(v.x, 4), round(1 - v.y, 4)]
    for name in ["mosaic"] + LABELLED:
        pts = device_corners(None if name == "mosaic" else [name])
        view = project(pts)
        right = max(view, key=lambda p: p.x)
        mid_y = (min(p.y for p in view) + max(p.y for p in view)) / 2
        out[name] = [round(right.x, 4), round(1 - mid_y, 4)]
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
        journey["anchors"]["a"].append(anchors())
        render(f"journey/a-{f:02d}.png")
    set_ring(1)
    render("journey/a-ring.png")

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
        hide(diorama, fade <= 0.001)
        e = 0.35 * smooth(span(f, 15, B_MOVE - 15)) + 0.65 * settle
        place_device(0.06 * smooth(span(f, 4, 20)), smooth(span(f, 10, 40)), e)
        journey["anchors"]["b"].append(anchors())
        render(f"journey/b-{f:02d}.png")

    with open(os.path.join(OUT, "journey.json"), "w") as fh:
        json.dump(journey, fh)
