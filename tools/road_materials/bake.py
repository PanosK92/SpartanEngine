"""Bake the island road materials from asphalt_track using DXC, SPIRV-Cross and OpenGL 4.3.

Run from the repository root with the Vulkan SDK tools on PATH and moderngl, Pillow,
and numpy installed. The renderer only needs the resulting textures.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

import moderngl
import numpy as np
from PIL import Image


def read_map(source, channel, size, mode):
    path = next(source.glob(f"asphalt_track_{channel}_4k.*"))
    with Image.open(path) as image:
        if image.mode.startswith("I"):
            image = Image.fromarray((np.asarray(image, dtype=np.uint16) >> 8).astype(np.uint8))
        image = image.convert(mode).resize((size, size), Image.Resampling.LANCZOS)
        return np.asarray(image)


def bake(ctx, source, output, paint):
    width, height, metres_x, metres_y = (128, 4096, .15, 12) if paint else (4096, 4096, 36, 27)
    strip = 128
    footprint = max(metres_x / width, metres_y / height)
    shader = Path(__file__).with_name("bake.hlsl")
    defines = dict(WIDTH=width, HEIGHT=height, STRIP_HEIGHT=strip, METRES_X=metres_x,
                   METRES_Y=metres_y, FOOTPRINT=footprint, PAINT=int(paint))
    with tempfile.TemporaryDirectory() as temp:
        spirv = Path(temp) / "bake.spv"
        subprocess.run([shutil.which("dxc"), "-T", "cs_6_0", "-E", "main_cs", "-spirv",
                        *[f"-D{k}={v}" for k, v in defines.items()], str(shader), "-Fo", str(spirv)], check=True)
        glsl = subprocess.check_output(["spirv-cross", str(spirv), "--version", "430"], text=True)
        program = ctx.compute_shader(glsl)

    size = round(1.5 / footprint)
    diffuse = read_map(source, "diff", size, "RGB")
    normals = read_map(source, "nor_gl", size, "RGB")
    low = read_map(source, "nor_gl", 16, "RGB")
    rough, ao, relief = [read_map(source, ch, size, "L") for ch in ("rough", "ao", "height")]
    result = [np.empty((height, width, 3), dtype=np.uint8) for _ in range(3)]
    count = width * strip
    inputs = np.empty((count + 1, 2), dtype=np.uint32)
    src = ctx.buffer(reserve=inputs.nbytes)
    dst = ctx.buffer(reserve=count * 16)
    src.bind_to_storage_buffer(0)
    dst.bind_to_storage_buffer(1)
    u = ((np.arange(width) + .5) / width - .5) * metres_x / 1.5

    def sample(img, v):
        # Periodic bilinear filtering at the output footprint.
        n = img.shape[0]
        x, y = u * n - .5, v * n - .5
        ix, iy = np.floor(x).astype(int), np.floor(y).astype(int)[:, None]
        fx, fy = (x - ix)[None, :], (y - iy[:, 0])[:, None]
        if img.ndim == 3:
            fx, fy = fx[..., None], fy[..., None]
        a = img[iy % n, ix % n] * (1-fx) + img[iy % n, (ix+1) % n] * fx
        b = img[(iy+1) % n, ix % n] * (1-fx) + img[(iy+1) % n, (ix+1) % n] * fx
        return np.rint(a*(1-fy) + b*fy).astype(np.uint32)

    for start in range(0, height, strip):
        v = (np.arange(start, start + strip) + .5) / height * metres_y / 1.5
        color = sample(diffuse, v)
        normal = np.clip(sample(normals, v).astype(int) - sample(low, v).astype(int) + 128, 0, 255).astype(np.uint32)
        inputs[:-1, 0] = (color[..., 0] | color[..., 1] << 8 | color[..., 2] << 16 | sample(rough, v) << 24).ravel()
        inputs[:-1, 1] = (normal[..., 0] | normal[..., 1] << 8 | sample(ao, v) << 16 | sample(relief, v) << 24).ravel()
        inputs[-1] = (start, 0)
        src.write(inputs)
        program.run(width // 8, strip // 8, 1)
        ctx.memory_barrier()
        pixels = np.frombuffer(dst.read(), np.uint32).reshape(strip, width, 4)
        for idx in range(3):
            result[idx][start:start+strip] = (pixels[..., idx, None] >> np.array([0,8,16], np.uint32)) & 255

    prefix = "paint" if paint else "asphalt"
    for name, data in [("diff", result[0]), ("nor_gl", result[1]), ("ao", result[2][...,0]),
                       ("rough", result[2][...,1]), ("height", result[2][...,2])]:
        Image.fromarray(data).save(output / f"{prefix}_{name}.jpg", quality=95, subsampling=0)
    src.release()
    dst.release()
    program.release()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("binaries/project/materials/island_roads"))
    parser.add_argument("--output", type=Path, default=Path("data/textures/island_roads"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    context = moderngl.create_standalone_context(require=430)
    for is_paint in (True, False):
        bake(context, args.source, args.output, is_paint)
