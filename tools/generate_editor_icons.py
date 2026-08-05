from math import cos, pi, sin
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


size = 64
scale = 4
white = (255, 255, 255, 255)
clear = (0, 0, 0, 0)
output_directory = Path(__file__).parents[1] / "data" / "icons"
font_regular = "C:/Windows/Fonts/arial.ttf"
font_bold = "C:/Windows/Fonts/arialbd.ttf"


class Icon:
    def __init__(self):
        self.image = Image.new(
            "RGBA",
            (size * scale, size * scale),
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

    def line(self, points, width=4):
        self.draw.line(
            [self.point(point) for point in points],
            fill=white,
            width=self.value(width),
            joint="curve"
        )

    def polygon(self, points, fill=None, width=4):
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

    def rectangle(self, box, radius=0, fill=None, width=4):
        self.draw.rounded_rectangle(
            self.box(box),
            radius=self.value(radius),
            fill=white if fill else None,
            outline=None if fill else white,
            width=self.value(width)
        )

    def ellipse(self, box, fill=None, width=4):
        self.draw.ellipse(
            self.box(box),
            fill=white if fill else None,
            outline=None if fill else white,
            width=self.value(width)
        )

    def arc(self, box, start, end, width=4):
        self.draw.arc(
            self.box(box),
            start=start,
            end=end,
            fill=white,
            width=self.value(width)
        )

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

    def save(self, name):
        image = self.image.resize(
            (size, size),
            Image.Resampling.LANCZOS
        )
        image.save(output_directory / f"{name}.png")
        return image


def draw_file(icon, label=None):
    icon.polygon([
        (14, 8),
        (38, 8),
        (50, 20),
        (50, 56),
        (14, 56)
    ])
    icon.line([(38, 8), (38, 20), (50, 20)])
    if label:
        icon.text((32, 39), label, 10 if len(label) > 2 else 14)


def draw_console():
    icon = Icon()
    icon.rectangle((8, 12, 56, 52), radius=5)
    icon.line([(18, 25), (25, 32), (18, 39)])
    icon.line([(31, 40), (44, 40)])
    return icon


def draw_file_plain():
    icon = Icon()
    draw_file(icon)
    icon.line([(22, 31), (42, 31)])
    icon.line([(22, 40), (37, 40)])
    return icon


def draw_folder():
    icon = Icon()
    icon.polygon([
        (7, 17),
        (25, 17),
        (30, 23),
        (57, 23),
        (52, 52),
        (7, 52)
    ])
    icon.line([(7, 17), (7, 52)])
    return icon


def draw_model():
    icon = Icon()
    icon.polygon([(32, 7), (51, 17), (32, 27), (13, 17)])
    icon.line([(13, 17), (13, 39), (32, 50), (32, 27)])
    icon.line([(51, 17), (51, 39), (32, 50)])
    return icon


def draw_world():
    icon = Icon()
    icon.ellipse((8, 8, 56, 56))
    icon.ellipse((20, 8, 44, 56))
    icon.line([(9, 32), (55, 32)])
    icon.arc((10, 17, 54, 47), 180, 360)
    icon.arc((10, 17, 54, 47), 0, 180)
    return icon


def draw_material():
    icon = Icon()
    icon.ellipse((9, 9, 55, 55))
    icon.arc((15, 15, 49, 49), 205, 72, width=5)
    icon.ellipse((21, 18, 28, 25), fill=True)
    return icon


def draw_labeled_file(label):
    icon = Icon()
    draw_file(icon, label)
    return icon


def draw_code():
    icon = Icon()
    draw_file(icon)
    icon.line([(28, 30), (22, 36), (28, 42)])
    icon.line([(36, 30), (42, 36), (36, 42)])
    return icon


def draw_shader():
    icon = Icon()
    draw_file(icon)
    icon.line([(24, 31), (40, 31), (32, 44), (24, 31)], width=3)
    icon.ellipse((20, 27, 28, 35), fill=True)
    icon.ellipse((36, 27, 44, 35), fill=True)
    icon.ellipse((28, 40, 36, 48), fill=True)
    return icon


def draw_font():
    icon = Icon()
    draw_file(icon)
    icon.text((32, 38), "A", 22)
    return icon


def draw_screenshot():
    icon = Icon()
    icon.rectangle((7, 15, 57, 51), radius=4)
    icon.line([(17, 15), (21, 9), (34, 9), (38, 15)])
    icon.ellipse((22, 23, 44, 45))
    icon.ellipse((48, 21, 51, 24), fill=True)
    return icon


def draw_gear():
    icon = Icon()
    points = []
    for index in range(24):
        angle = -pi / 2 + index * pi / 12
        radius = 25 if index % 3 == 1 else 20
        points.append((
            32 + cos(angle) * radius,
            32 + sin(angle) * radius
        ))
    icon.polygon(points, fill=True)
    icon.ellipse((23, 23, 41, 41), fill=False, width=0)
    icon.draw.ellipse(icon.box((23, 23, 41, 41)), fill=clear)
    return icon


def draw_play():
    icon = Icon()
    icon.polygon([(20, 11), (52, 32), (20, 53)], fill=True)
    return icon


def draw_pause():
    icon = Icon()
    icon.rectangle((16, 11, 27, 53), radius=2, fill=True)
    icon.rectangle((37, 11, 48, 53), radius=2, fill=True)
    return icon


def draw_timer():
    icon = Icon()
    icon.ellipse((10, 13, 54, 57))
    icon.line([(32, 13), (32, 7)])
    icon.line([(25, 7), (39, 7)])
    icon.line([(32, 22), (32, 34), (41, 40)])
    icon.line([(48, 16), (53, 11)])
    return icon


def draw_resource_viewer():
    icon = Icon()
    icon.ellipse((11, 9, 53, 25))
    icon.arc((11, 17, 53, 35), 0, 180)
    icon.arc((11, 29, 53, 47), 0, 180)
    icon.arc((11, 41, 53, 57), 0, 180)
    icon.line([(11, 17), (11, 49)])
    icon.line([(53, 17), (53, 49)])
    return icon


def draw_renderdoc():
    icon = Icon()
    icon.line([(21, 9), (9, 9), (9, 21)])
    icon.line([(43, 9), (55, 9), (55, 21)])
    icon.line([(9, 43), (9, 55), (21, 55)])
    icon.line([(55, 43), (55, 55), (43, 55)])
    icon.polygon([(32, 18), (45, 25), (32, 32), (19, 25)])
    icon.line([(19, 25), (19, 40), (32, 47), (32, 32)])
    icon.line([(45, 25), (45, 40), (32, 47)])
    return icon


def draw_texture():
    icon = Icon()
    for row in range(3):
        for column in range(3):
            x = 9 + column * 15
            y = 9 + row * 15
            icon.rectangle(
                (x, y, x + 15, y + 15),
                fill=(row + column) % 2 == 0
            )
    return icon


def draw_minimize():
    icon = Icon()
    icon.line([(14, 39), (50, 39)], width=5)
    return icon


def draw_maximize():
    icon = Icon()
    icon.rectangle((14, 14, 50, 50), radius=2)
    return icon


def draw_close():
    icon = Icon()
    icon.line([(16, 16), (48, 48)], width=5)
    icon.line([(48, 16), (16, 48)], width=5)
    return icon


def draw_entity():
    icon = Icon()
    icon.rectangle((25, 7, 39, 21), radius=3)
    icon.rectangle((7, 43, 21, 57), radius=3)
    icon.rectangle((25, 43, 39, 57), radius=3)
    icon.rectangle((43, 43, 57, 57), radius=3)
    icon.line([(32, 21), (32, 35), (14, 35), (14, 43)])
    icon.line([(32, 35), (32, 43)])
    icon.line([(32, 35), (50, 35), (50, 43)])
    return icon


def draw_hybrid():
    icon = Icon()
    icon.ellipse((8, 17, 38, 47))
    icon.rectangle((27, 17, 56, 47), radius=4)
    icon.line([(32, 22), (32, 42)])
    return icon


def draw_audio():
    icon = Icon()
    icon.polygon([(8, 26), (20, 26), (34, 14), (34, 50), (20, 38), (8, 38)])
    icon.arc((31, 20, 51, 44), 285, 75)
    icon.arc((29, 13, 59, 51), 285, 75)
    return icon


def draw_terrain():
    icon = Icon()
    icon.polygon([
        (6, 49),
        (20, 27),
        (28, 37),
        (39, 18),
        (58, 49)
    ])
    icon.line([(12, 49), (24, 39), (31, 44), (45, 31), (54, 42)])
    return icon


def draw_light():
    icon = Icon()
    icon.ellipse((17, 10, 47, 40))
    icon.line([(23, 38), (23, 46), (41, 46), (41, 38)])
    icon.line([(26, 53), (38, 53)])
    for angle in range(0, 360, 45):
        radians = angle * pi / 180
        start = (
            32 + cos(radians) * 24,
            25 + sin(radians) * 24
        )
        end = (
            32 + cos(radians) * 29,
            25 + sin(radians) * 29
        )
        icon.line([start, end], width=3)
    return icon


def draw_camera():
    icon = Icon()
    icon.rectangle((7, 18, 44, 48), radius=4)
    icon.polygon([(44, 26), (57, 19), (57, 47), (44, 40)], fill=True)
    icon.ellipse((16, 26, 28, 38))
    return icon


def draw_particle():
    icon = Icon()
    icon.ellipse((28, 28, 36, 36), fill=True)
    icon.ellipse((11, 15, 18, 22), fill=True)
    icon.ellipse((45, 11, 52, 18), fill=True)
    icon.ellipse((47, 44, 56, 53), fill=True)
    icon.line([(32, 23), (32, 9)])
    icon.line([(23, 28), (11, 34)])
    icon.line([(39, 27), (51, 25)])
    icon.line([(37, 39), (43, 52)])
    icon.line([(26, 39), (19, 51)])
    return icon


def draw_physics():
    icon = Icon()
    icon.ellipse((27, 27, 37, 37), fill=True)
    icon.ellipse((9, 23, 55, 41))
    icon.draw.ellipse(
        icon.box((9, 23, 55, 41)),
        outline=white,
        width=icon.value(3)
    )
    for angle in (60, 120):
        layer = Image.new("RGBA", icon.image.size, clear)
        layer_draw = ImageDraw.Draw(layer)
        layer_draw.ellipse(
            icon.box((9, 23, 55, 41)),
            outline=white,
            width=icon.value(3)
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
    for index, y in enumerate(range(25, 45, 5)):
        x = 29 if index % 2 == 0 else 35
        icon.rectangle((x, y, x + 6, y + 5), fill=True)
    icon.rectangle((29, 46, 41, 51), radius=1)
    return icon


def draw_arrow(direction):
    icon = Icon()
    if direction == "left":
        points = [(48, 14), (30, 32), (48, 50)]
    elif direction == "right":
        points = [(16, 14), (34, 32), (16, 50)]
    else:
        points = [(14, 44), (32, 26), (50, 44)]
    icon.line(points, width=6)
    return icon


def draw_refresh():
    icon = Icon()
    icon.arc((10, 10, 54, 54), 35, 205, width=5)
    icon.arc((10, 10, 54, 54), 215, 385, width=5)
    icon.polygon([(47, 8), (57, 17), (44, 21)], fill=True)
    icon.polygon([(17, 56), (7, 47), (20, 43)], fill=True)
    return icon


def draw_mcp():
    icon = Icon()
    icon.ellipse((27, 7, 37, 17), fill=True)
    icon.ellipse((8, 43, 18, 53), fill=True)
    icon.ellipse((27, 47, 37, 57), fill=True)
    icon.ellipse((46, 43, 56, 53), fill=True)
    icon.line([(32, 17), (32, 31)])
    icon.line([(32, 31), (13, 43)])
    icon.line([(32, 31), (32, 47)])
    icon.line([(32, 31), (51, 43)])
    return icon


def draw_snap():
    icon = Icon()
    icon.line([(13, 10), (13, 34)])
    icon.line([(51, 10), (51, 34)])
    icon.arc((13, 20, 51, 58), 0, 180, width=9)
    icon.line([(13, 10), (25, 10)], width=9)
    icon.line([(39, 10), (51, 10)], width=9)
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
    "camera": draw_camera,
    "particle": draw_particle,
    "physics": draw_physics,
    "compressed": draw_compressed,
    "arrow_left": lambda: draw_arrow("left"),
    "arrow_right": lambda: draw_arrow("right"),
    "arrow_up": lambda: draw_arrow("up"),
    "refresh": draw_refresh,
    "mcp": draw_mcp,
    "snap": draw_snap
}


def generate():
    output_directory.mkdir(parents=True, exist_ok=True)
    generated = []
    for name, draw_icon in icons.items():
        generated.append((name, draw_icon().save(name)))

    columns = 6
    cell_width = 110
    rows = (len(generated) + columns - 1) // columns
    preview = Image.new(
        "RGBA",
        (columns * cell_width, rows * 92),
        (25, 27, 31, 255)
    )
    preview_draw = ImageDraw.Draw(preview)
    font = ImageFont.truetype(font_regular, 10)
    for index, (name, image) in enumerate(generated):
        column = index % columns
        row = index // columns
        x = column * cell_width + 23
        y = row * 92 + 5
        preview.alpha_composite(image, (x, y))
        preview_draw.text(
            (column * cell_width + cell_width / 2, y + 69),
            name,
            fill=white,
            font=font,
            anchor="ma"
        )
    preview.save(output_directory / "_preview.png")


if __name__ == "__main__":
    generate()
