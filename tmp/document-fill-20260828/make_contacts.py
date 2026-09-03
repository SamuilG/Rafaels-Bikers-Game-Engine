from pathlib import Path

from PIL import Image, ImageDraw


source_dir = Path(r"D:\code\GroupProject\Rafaels-Bikers-Game-Engine\tmp\document-fill-20260828\pdf-render")
output_dir = source_dir / "contacts"
output_dir.mkdir(exist_ok=True)
files = sorted(source_dir.glob("page-*.png"))
thumb = (850, 600)

for group_start in range(0, len(files), 4):
    tiles = []
    for path in files[group_start : group_start + 4]:
        image = Image.open(path).convert("RGB")
        image.thumbnail(thumb)
        tile = Image.new("RGB", (thumb[0], thumb[1] + 40), "white")
        tile.paste(image, ((thumb[0] - image.width) // 2, 40))
        ImageDraw.Draw(tile).text((10, 10), path.stem, fill="black")
        tiles.append(tile)

    sheet = Image.new("RGB", (thumb[0] * 2, (thumb[1] + 40) * 2), "white")
    for index, tile in enumerate(tiles):
        sheet.paste(tile, ((index % 2) * thumb[0], (index // 2) * (thumb[1] + 40)))
    sheet.save(output_dir / f"contact-{group_start // 4 + 1}.jpg", quality=90)

print(f"pages={len(files)} contact_sheets={len(list(output_dir.glob('*.jpg')))}")
