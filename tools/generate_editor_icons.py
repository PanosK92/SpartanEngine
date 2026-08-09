from math import cos, pi, sin
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


# 128 is sharp at 48px viewport icons and 2x dpi ui sizes
size = 128
scale = 4
# logical canvas is 0..64 so geometry stays easy to reason about
logical = 64
white = (255, 255, 255, 255)
clear = (0, 0, 0, 0)
# shared stroke language for the whole set
stroke = 4.0
stroke_thin = 3.0
stroke_thick = 5.0
root_directory = Path(__file__).parents[1]
output_directory = root_directory / "data" / "icons"
binaries_icons = root_directory / "binaries" / "data" / "icons"
font_regular = "C:/Windows/Fonts/arial.ttf"
font_bold = "C:/Windows/Fonts/arialbd.ttf"


class Icon:
    def __init__(self):
        self.image = Image.new(
            "RGBA",
            (logical * scale, logical * scale),
            clear
        )
        self.draw = ImageDraw.Draw(self.image)

    @staticmethod
    def value(value):
        return round(value * scale)

    @classmethod
    def point(cls, point):
        return tuple(cls.value(value) for value in point)

    @classmethod
    def box(cls, box):
        return tuple(cls.value(value) for value in box)

    def line(self, points, width=stroke):
        self.draw.line(
            [self.point(point) for point in points],
            fill=white,
            width=self.value(width),
            joint="curve"
        )

    def polygon(self, points, fill=False, width=stroke):
        points = [self.point(point) for point in points]
        if fill:
            self.draw.polygon(points, fill=white)
        else:
            self.draw.line(
                points + [points[0]],
                fill=white,
                width=self.value(width),
                joint="curve"
            )

    def rectangle(self, box, radius=0, fill=False, width=stroke):
        self.draw.rounded_rectangle(
            self.box(box),
            radius=self.value(radius),
            fill=white if fill else None,
            outline=None if fill else white,
            width=self.value(width)
        )

    def ellipse(self, box, fill=False, width=stroke):
        self.draw.ellipse(
            self.box(box),
            fill=white if fill else None,
            outline=None if fill else white,
            width=self.value(width)
        )

    def arc(self, box, start, end, width=stroke):
        self.draw.arc(
            self.box(box),
            start=start,
            end=end,
            fill=white,
            width=self.value(width)
        )

    def clear_ellipse(self, box):
        self.draw.ellipse(self.box(box), fill=clear)

    def text(self, position, text, font_size):
        font = ImageFont.truetype(
            font_bold,
            self.value(font_size)
        )
        self.draw.text(
            self.point(position),
            text,
            fill=white,
            font=font,
            anchor="mm"
        )

    def finalize(self):
        image = self.image.resize(
            (size, size),
            Image.Resampling.LANCZOS
        )
        # force pure white rgb, keep alpha from downsample
        pixels = image.load()
        width, height = image.size
        for y in range(height):
            for x in range(width):
                r, g, b, a = pixels[x, y]
                if a > 0:
                    pixels[x, y] = (255, 255, 255, a)
        return image

    def save(self, name, directory=None):
        image = self.finalize()
        target = directory or output_directory
        target.mkdir(parents=True, exist_ok=True)
        image.save(target / f"{name}.png")
        return image


def draw_file(icon, label=None):
    icon.polygon([
        (16, 8),
        (38, 8),
        (48, 18),
        (48, 56),
        (16, 56)
    ])
    icon.line([(38, 8), (38, 18), (48, 18)], width=stroke_thin)
    if label:
        icon.text((32, 40), label, 9 if len(label) > 2 else 13)


def draw_console():
    icon = Icon()
    icon.rectangle((9, 12, 55, 52), radius=5)
    icon.line([(18, 25), (26, 32), (18, 39)])
    icon.line([(30, 40), (44, 40)])
    return icon


def draw_file_plain():
    icon = Icon()
    draw_file(icon)
    icon.line([(24, 30), (40, 30)], width=stroke_thin)
    icon.line([(24, 38), (36, 38)], width=stroke_thin)
    icon.line([(24, 46), (40, 46)], width=stroke_thin)
    return icon


def draw_folder():
    icon = Icon()
    icon.polygon([
        (8, 18),
        (24, 18),
        (29, 24),
        (56, 24),
        (56, 52),
        (8, 52)
    ])
    icon.line([(8, 24), (56, 24)], width=stroke_thin)
    return icon


def draw_model():
    icon = Icon()
    icon.polygon([(32, 8), (52, 19), (32, 30), (12, 19)])
    icon.line([(12, 19), (12, 41), (32, 52)])
    icon.line([(52, 19), (52, 41), (32, 52)])
    icon.line([(32, 30), (32, 52)], width=stroke_thin)
    return icon


def draw_world():
    icon = Icon()
    icon.ellipse((8, 8, 56, 56))
    icon.ellipse((20, 8, 44, 56), width=stroke_thin)
    icon.line([(9, 32), (55, 32)], width=stroke_thin)
    icon.arc((10, 18, 54, 46), 180, 360, width=stroke_thin)
    icon.arc((10, 18, 54, 46), 0, 180, width=stroke_thin)
    return icon


def draw_material():
    icon = Icon()
    icon.ellipse((9, 9, 55, 55))
    icon.arc((16, 16, 48, 48), 210, 80, width=stroke_thick)
    icon.ellipse((22, 19, 29, 26), fill=True)
    return icon


def draw_labeled_file(label):
    icon = Icon()
    draw_file(icon, label)
    return icon


def draw_code():
    icon = Icon()
    draw_file(icon)
    icon.line([(28, 30), (22, 37), (28, 44)])
    icon.line([(36, 30), (42, 37), (36, 44)])
    return icon


def draw_shader():
    icon = Icon()
    draw_file(icon)
    icon.line([(24, 32), (40, 32), (32, 44), (24, 32)], width=stroke_thin)
    icon.ellipse((21, 28, 28, 35), fill=True)
    icon.ellipse((36, 28, 43, 35), fill=True)
    icon.ellipse((28, 41, 35, 48), fill=True)
    return icon


def draw_font():
    icon = Icon()
    draw_file(icon)
    icon.text((32, 39), "A", 20)
    return icon


def draw_screenshot():
    icon = Icon()
    icon.rectangle((8, 18, 56, 50), radius=4)
    icon.line([(18, 18), (22, 10), (34, 10), (38, 18)])
    icon.ellipse((23, 25, 41, 43))
    icon.ellipse((46, 23, 50, 27), fill=True)
    return icon


def draw_gear():
    icon = Icon()
    teeth = 8
    outer = 26.0
    valley = 19.0
    points = []
    for index in range(teeth):
        base = -pi / 2 + index * (2 * pi / teeth)
        # flat topped tooth then valley
        tip_a = base + (2 * pi / teeth) * 0.18
        tip_b = base + (2 * pi / teeth) * 0.38
        valley_a = base + (2 * pi / teeth) * 0.52
        valley_b = base + (2 * pi / teeth) * 0.88
        for angle, radius in (
            (base, valley),
            (tip_a, outer),
            (tip_b, outer),
            (valley_a, valley),
            (valley_b, valley)
        ):
            points.append((
                32 + cos(angle) * radius,
                32 + sin(angle) * radius
            ))
    icon.polygon(points, fill=True)
    icon.clear_ellipse((23, 23, 41, 41))
    return icon


def draw_play():
    icon = Icon()
    icon.polygon([(20, 12), (50, 32), (20, 52)], fill=True)
    return icon


def draw_pause():
    icon = Icon()
    icon.rectangle((17, 12, 27, 52), radius=2, fill=True)
    icon.rectangle((37, 12, 47, 52), radius=2, fill=True)
    return icon


def draw_timer():
    icon = Icon()
    icon.ellipse((10, 14, 54, 58))
    icon.line([(32, 14), (32, 8)], width=stroke_thick)
    icon.line([(24, 8), (40, 8)], width=stroke_thick)
    icon.line([(32, 24), (32, 35), (41, 41)])
    icon.line([(46, 18), (52, 12)], width=stroke_thin)
    return icon


def draw_resource_viewer():
    icon = Icon()
    icon.ellipse((12, 10, 52, 24))
    icon.line([(12, 17), (12, 48)])
    icon.line([(52, 17), (52, 48)])
    icon.arc((12, 20, 52, 36), 0, 180)
    icon.arc((12, 32, 52, 48), 0, 180)
    icon.arc((12, 42, 52, 58), 0, 180)
    return icon


def draw_renderdoc():
    icon = Icon()
    # corner brackets
    for x0, y0, x1, y1 in (
        (10, 10, 22, 10),
        (10, 10, 10, 22),
        (42, 10, 54, 10),
        (54, 10, 54, 22),
        (10, 42, 10, 54),
        (10, 54, 22, 54),
        (54, 42, 54, 54),
        (42, 54, 54, 54)
    ):
        icon.line([(x0, y0), (x1, y1)])
    # isometric cube
    icon.polygon([(32, 20), (44, 27), (32, 34), (20, 27)])
    icon.line([(20, 27), (20, 40), (32, 47)])
    icon.line([(44, 27), (44, 40), (32, 47)])
    icon.line([(32, 34), (32, 47)], width=stroke_thin)
    return icon


def draw_texture():
    icon = Icon()
    icon.rectangle((10, 10, 54, 54), radius=2)
    icon.line([(32, 10), (32, 54)], width=stroke_thin)
    icon.line([(10, 32), (54, 32)], width=stroke_thin)
    icon.rectangle((10, 10, 32, 32), fill=True)
    icon.rectangle((32, 32, 54, 54), fill=True)
    return icon


def draw_minimize():
    icon = Icon()
    icon.line([(14, 40), (50, 40)], width=stroke_thick)
    return icon


def draw_maximize():
    icon = Icon()
    icon.rectangle((14, 14, 50, 50), radius=2)
    return icon


def draw_close():
    icon = Icon()
    icon.line([(16, 16), (48, 48)], width=stroke_thick)
    icon.line([(48, 16), (16, 48)], width=stroke_thick)
    return icon


def draw_entity():
    icon = Icon()
    icon.rectangle((25, 8, 39, 22), radius=3)
    icon.rectangle((8, 42, 22, 56), radius=3)
    icon.rectangle((25, 42, 39, 56), radius=3)
    icon.rectangle((42, 42, 56, 56), radius=3)
    icon.line([(32, 22), (32, 34)])
    icon.line([(15, 34), (49, 34)])
    icon.line([(15, 34), (15, 42)])
    icon.line([(32, 34), (32, 42)])
    icon.line([(49, 34), (49, 42)])
    return icon


def draw_hybrid():
    icon = Icon()
    icon.ellipse((8, 16, 40, 48))
    icon.rectangle((26, 16, 56, 48), radius=4)
    return icon


def draw_audio():
    icon = Icon()
    icon.polygon([
        (8, 26),
        (20, 26),
        (32, 14),
        (32, 50),
        (20, 38),
        (8, 38)
    ], fill=True)
    icon.arc((28, 24, 42, 40), 305, 55, width=stroke_thin)
    icon.arc((26, 18, 48, 46), 305, 55)
    icon.arc((24, 12, 54, 52), 305, 55)
    return icon


def draw_terrain():
    icon = Icon()
    icon.polygon([
        (8, 50),
        (22, 24),
        (30, 36),
        (42, 14),
        (56, 50)
    ])
    icon.line([(8, 50), (56, 50)])
    return icon


def draw_light():
    icon = Icon()
    # bulb
    icon.ellipse((18, 8, 46, 36))
    # neck
    icon.line([(24, 34), (24, 42), (40, 42), (40, 34)])
    # base rings
    icon.line([(26, 46), (38, 46)], width=stroke_thin)
    icon.line([(28, 50), (36, 50)], width=stroke_thin)
    # rays
    for angle in (270, 225, 315, 180, 0):
        radians = angle * pi / 180
        start = (
            32 + cos(radians) * 22,
            22 + sin(radians) * 22
        )
        end = (
            32 + cos(radians) * 28,
            22 + sin(radians) * 28
        )
        icon.line([start, end], width=stroke_thin)
    return icon


def draw_camera():
    icon = Icon()
    icon.rectangle((8, 18, 42, 48), radius=4)
    icon.polygon([(42, 24), (56, 16), (56, 50), (42, 42)], fill=True)
    icon.ellipse((16, 26, 30, 40))
    return icon


def draw_particle():
    icon = Icon()
    icon.ellipse((28, 28, 36, 36), fill=True)
    satellites = (
        (12, 14, 19, 21),
        (45, 10, 52, 17),
        (48, 44, 56, 52),
        (10, 42, 17, 49)
    )
    for box in satellites:
        icon.ellipse(box, fill=True)
    icon.line([(32, 24), (32, 10)], width=stroke_thin)
    icon.line([(24, 28), (12, 34)], width=stroke_thin)
    icon.line([(40, 28), (52, 24)], width=stroke_thin)
    icon.line([(36, 38), (44, 52)], width=stroke_thin)
    icon.line([(26, 38), (18, 50)], width=stroke_thin)
    return icon


def draw_physics():
    icon = Icon()
    icon.ellipse((27, 27, 37, 37), fill=True)
    for angle in (0, 60, 120):
        layer = Image.new("RGBA", icon.image.size, clear)
        layer_draw = ImageDraw.Draw(layer)
        layer_draw.ellipse(
            icon.box((10, 22, 54, 42)),
            outline=white,
            width=icon.value(stroke_thin)
        )
        layer = layer.rotate(
            angle,
            resample=Image.Resampling.BICUBIC,
            center=icon.point((32, 32))
        )
        icon.image.alpha_composite(layer)
    return icon


def draw_compressed():
    icon = Icon()
    draw_file(icon)
    for index, y in enumerate(range(26, 48, 5)):
        x = 28 if index % 2 == 0 else 34
        icon.rectangle((x, y, x + 6, y + 4), fill=True)
    icon.rectangle((28, 48, 40, 52), radius=1, fill=True)
    return icon


def draw_arrow(direction):
    icon = Icon()
    if direction == "left":
        points = [(46, 14), (22, 32), (46, 50)]
    elif direction == "right":
        points = [(18, 14), (42, 32), (18, 50)]
    else:
        points = [(14, 44), (32, 22), (50, 44)]
    icon.line(points, width=stroke_thick)
    return icon


def draw_refresh():
    icon = Icon()
    icon.arc((10, 10, 54, 54), 40, 200, width=stroke_thick)
    icon.arc((10, 10, 54, 54), 220, 380, width=stroke_thick)
    icon.polygon([(46, 8), (56, 16), (44, 20)], fill=True)
    icon.polygon([(18, 56), (8, 48), (20, 44)], fill=True)
    return icon


def draw_mcp():
    icon = Icon()
    icon.ellipse((27, 8, 37, 18), fill=True)
    icon.ellipse((8, 44, 18, 54), fill=True)
    icon.ellipse((27, 46, 37, 56), fill=True)
    icon.ellipse((46, 44, 56, 54), fill=True)
    icon.line([(32, 18), (32, 30)])
    icon.line([(32, 30), (13, 44)])
    icon.line([(32, 30), (32, 46)])
    icon.line([(32, 30), (51, 44)])
    return icon


def draw_snap():
    icon = Icon()
    icon.line([(14, 10), (14, 34)])
    icon.line([(50, 10), (50, 34)])
    icon.arc((14, 18, 50, 56), 0, 180, width=stroke_thick)
    icon.line([(14, 10), (24, 10)], width=stroke_thick)
    icon.line([(40, 10), (50, 10)], width=stroke_thick)
    return icon


def draw_light_point():
    icon = Icon()
    icon.ellipse((16, 8, 48, 40))
    icon.line([(32, 18), (32, 30)], width=stroke_thin)
    icon.line([(26, 24), (32, 30), (38, 24)], width=stroke_thin)
    icon.line([(24, 38), (24, 46), (40, 46), (40, 38)])
    icon.line([(26, 50), (38, 50)], width=stroke_thin)
    icon.line([(28, 54), (36, 54)], width=stroke_thin)
    return icon


def draw_light_spot():
    icon = Icon()
    icon.rectangle((8, 26, 34, 40), radius=3, fill=True)
    icon.polygon([
        (32, 22),
        (50, 14),
        (50, 50),
        (32, 42)
    ], fill=True)
    icon.clear_ellipse((42, 26, 48, 38))
    for y in (16, 24, 32, 40, 48):
        icon.line([(52, y), (58, y)], width=stroke_thin)
    return icon


def draw_light_directional():
    icon = Icon()
    icon.ellipse((20, 20, 44, 44), fill=True)
    for index in range(8):
        angle = -pi / 2 + index * (pi / 4)
        start = (
            32 + cos(angle) * 18,
            32 + sin(angle) * 18
        )
        end = (
            32 + cos(angle) * 28,
            32 + sin(angle) * 28
        )
        icon.line([start, end], width=stroke_thick)
    return icon


def draw_volume():
    icon = Icon()
    icon.rectangle((12, 12, 52, 52), radius=2)
    icon.line([(12, 32), (52, 32)], width=stroke_thin)
    icon.line([(32, 12), (32, 52)], width=stroke_thin)
    return icon


def draw_script():
    icon = Icon()
    icon.rectangle((12, 10, 52, 54), radius=4)
    icon.line([(22, 24), (16, 32), (22, 40)])
    icon.line([(42, 24), (48, 32), (42, 40)])
    icon.line([(34, 22), (28, 42)], width=stroke_thin)
    return icon


def draw_spline():
    icon = Icon()
    icon.ellipse((10, 38, 18, 46), fill=True)
    icon.ellipse((28, 10, 36, 18), fill=True)
    icon.ellipse((46, 38, 54, 46), fill=True)
    icon.arc((10, 10, 54, 54), 200, 340)
    return icon


def draw_spline_follower():
    icon = Icon()
    icon.arc((10, 18, 54, 54), 200, 340)
    icon.polygon([(46, 18), (56, 28), (42, 30)], fill=True)
    icon.ellipse((12, 40, 20, 48), fill=True)
    return icon


def draw_skid_marks():
    icon = Icon()
    icon.arc((8, 16, 40, 52), 40, 160, width=stroke_thick)
    icon.arc((24, 12, 56, 48), 40, 160, width=stroke_thick)
    return icon


def draw_water():
    icon = Icon()
    icon.arc((8, 18, 28, 34), 200, 340)
    icon.arc((20, 18, 40, 34), 200, 340)
    icon.arc((32, 18, 52, 34), 200, 340)
    icon.arc((8, 34, 28, 50), 200, 340)
    icon.arc((20, 34, 40, 50), 200, 340)
    icon.arc((32, 34, 52, 50), 200, 340)
    return icon


def draw_traffic():
    icon = Icon()
    icon.rectangle((12, 24, 52, 42), radius=4, fill=True)
    icon.rectangle((18, 16, 30, 24), radius=2, fill=True)
    icon.rectangle((34, 16, 46, 24), radius=2, fill=True)
    icon.ellipse((16, 40, 26, 50), fill=True)
    icon.ellipse((38, 40, 48, 50), fill=True)
    icon.clear_ellipse((18, 42, 24, 48))
    icon.clear_ellipse((40, 42, 46, 48))
    return icon


def draw_pedestrians():
    icon = Icon()
    icon.ellipse((26, 8, 38, 20), fill=True)
    icon.line([(32, 20), (32, 38)], width=stroke_thick)
    icon.line([(20, 26), (44, 26)], width=stroke_thick)
    icon.line([(32, 38), (22, 54)], width=stroke_thick)
    icon.line([(32, 38), (42, 54)], width=stroke_thick)
    return icon


def draw_spawn_point():
    icon = Icon()
    icon.ellipse((12, 12, 52, 52))
    icon.ellipse((24, 24, 40, 40))
    icon.ellipse((29, 29, 35, 35), fill=True)
    icon.line([(32, 8), (32, 16)], width=stroke_thin)
    icon.line([(32, 48), (32, 56)], width=stroke_thin)
    icon.line([(8, 32), (16, 32)], width=stroke_thin)
    icon.line([(48, 32), (56, 32)], width=stroke_thin)
    return icon


def draw_car_reset():
    icon = Icon()
    icon.arc((10, 10, 54, 54), 40, 300, width=stroke_thick)
    icon.polygon([(48, 8), (58, 16), (46, 20)], fill=True)
    icon.rectangle((20, 28, 44, 40), radius=2, fill=True)
    return icon


def draw_text_3d():
    icon = Icon()
    icon.rectangle((10, 14, 54, 50), radius=3)
    icon.text((32, 34), "T", 24)
    return icon


def draw_animator():
    icon = Icon()
    icon.ellipse((14, 14, 28, 28), fill=True)
    icon.ellipse((36, 14, 50, 28), fill=True)
    icon.ellipse((14, 36, 28, 50), fill=True)
    icon.ellipse((36, 36, 50, 50), fill=True)
    icon.line([(28, 21), (36, 21)])
    icon.line([(21, 28), (21, 36)])
    icon.line([(43, 28), (43, 36)])
    icon.line([(28, 43), (36, 43)])
    return icon


def draw_ragdoll():
    icon = Icon()
    icon.ellipse((26, 8, 38, 20))
    icon.line([(32, 20), (32, 36)])
    icon.line([(18, 24), (32, 28), (46, 24)])
    icon.line([(20, 52), (32, 36), (44, 52)])
    return icon


icons = {
    "console": draw_console,
    "file": draw_file_plain,
    "folder": draw_folder,
    "model": draw_model,
    "world": draw_world,
    "material": draw_material,
    "code": draw_code,
    "shader": draw_shader,
    "xml": lambda: draw_labeled_file("XML"),
    "dll": lambda: draw_labeled_file("DLL"),
    "txt": lambda: draw_labeled_file("TXT"),
    "ini": lambda: draw_labeled_file("INI"),
    "exe": lambda: draw_labeled_file("EXE"),
    "font": draw_font,
    "screenshot": draw_screenshot,
    "gear": draw_gear,
    "play": draw_play,
    "pause": draw_pause,
    "timer": draw_timer,
    "resource_viewer": draw_resource_viewer,
    "renderdoc": draw_renderdoc,
    "texture": draw_texture,
    "window_minimise": draw_minimize,
    "window_maximise": draw_maximize,
    "window_close": draw_close,
    "entity": draw_entity,
    "hybrid": draw_hybrid,
    "audio": draw_audio,
    "terrain": draw_terrain,
    "light": draw_light,
    "light_point": draw_light_point,
    "light_spot": draw_light_spot,
    "light_directional": draw_light_directional,
    "camera": draw_camera,
    "particle": draw_particle,
    "physics": draw_physics,
    "volume": draw_volume,
    "script": draw_script,
    "spline": draw_spline,
    "spline_follower": draw_spline_follower,
    "skid_marks": draw_skid_marks,
    "water": draw_water,
    "traffic": draw_traffic,
    "pedestrians": draw_pedestrians,
    "spawn_point": draw_spawn_point,
    "car_reset": draw_car_reset,
    "text_3d": draw_text_3d,
    "animator": draw_animator,
    "ragdoll": draw_ragdoll,
    "compressed": draw_compressed,
    "arrow_left": lambda: draw_arrow("left"),
    "arrow_right": lambda: draw_arrow("right"),
    "arrow_up": lambda: draw_arrow("up"),
    "refresh": draw_refresh,
    "mcp": draw_mcp,
    "snap": draw_snap
}


def sync_copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(source.read_bytes())


def generate():
    output_directory.mkdir(parents=True, exist_ok=True)
    generated = []
    for name, draw_icon in icons.items():
        image = draw_icon().save(name)
        generated.append((name, image))
        if binaries_icons.exists() or binaries_icons.parent.exists():
            sync_copy(output_directory / f"{name}.png", binaries_icons / f"{name}.png")

    columns = 6
    cell_width = 140
    cell_height = 120
    rows = (len(generated) + columns - 1) // columns
    preview = Image.new(
        "RGBA",
        (columns * cell_width, rows * cell_height),
        (25, 27, 31, 255)
    )
    preview_draw = ImageDraw.Draw(preview)
    font = ImageFont.truetype(font_regular, 11)
    for index, (name, image) in enumerate(generated):
        column = index % columns
        row = index // columns
        x = column * cell_width + 6
        y = row * cell_height + 4
        thumb = image.resize((64, 64), Image.Resampling.LANCZOS)
        preview.alpha_composite(thumb, (x + 38, y))
        preview_draw.text(
            (column * cell_width + cell_width / 2, y + 72),
            name,
            fill=white,
            font=font,
            anchor="ma"
        )
    preview.save(output_directory / "_preview.png")
    if binaries_icons.exists() or binaries_icons.parent.exists():
        sync_copy(output_directory / "_preview.png", binaries_icons / "_preview.png")

    print(f"generated {len(generated)} icons at {size}x{size} -> {output_directory}")


if __name__ == "__main__":
    generate()
