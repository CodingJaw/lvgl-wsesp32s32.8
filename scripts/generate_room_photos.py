Import("env")
from pathlib import Path
import io, urllib.request
from PIL import Image, ImageOps

root = Path(env["PROJECT_DIR"])
out = root / "waveshare-v2-showcase" / "src" / "room_photos.h"
photos = [
 ("entertainment", "Living_room_view_into_kitchen.jpg"),
 ("bedroom", "Rustic_wood_cabin_interior_with_dining_table_kitchenette_and_bedroom_area.jpg"),
 ("kitchen", "Kitchen_as_seen_from_living_room_door.jpg"),
 ("patio", "Living_room_view_to_porch_and_dining_area.jpg"),
]
# Generated once; keep the header between builds. Regenerate by deleting it.
if not out.exists():
    arrays = ["#pragma once", "#include <stdint.h>", "struct RoomPhoto { const uint8_t *data; uint32_t size; };"]
    png_entries, jpg_entries = [], []
    for name, filename in photos:
        url = "https://commons.wikimedia.org/wiki/Special:Redirect/file/" + filename
        request = urllib.request.Request(url, headers={"User-Agent": "ESP32RoomBenchmark/1.0 (educational display test)"})
        with urllib.request.urlopen(request, timeout=40) as response:
            raw = response.read()
        image = Image.open(io.BytesIO(raw)).convert("RGB")
        image = ImageOps.fit(image, (240, 320), method=Image.Resampling.LANCZOS)
        for fmt, suffix, options in [("PNG", "png", {"optimize": True}), ("JPEG", "jpg", {"quality": 85, "subsampling": 0})]:
            data = io.BytesIO()
            image.save(data, format=fmt, **options)
            blob = data.getvalue()
            ident = f"room_{name}_{suffix}"
            arrays.append(f"static const uint8_t {ident}[] = {{")
            arrays.extend("  " + ",".join(str(b) for b in blob[i:i+24]) + "," for i in range(0, len(blob), 24))
            arrays.append("};")
            (png_entries if suffix == "png" else jpg_entries).append(f"{{{ident}, sizeof({ident})}}")
    arrays.append("static const RoomPhoto room_png[4] = {" + ",".join(png_entries) + "};")
    arrays.append("static const RoomPhoto room_jpg[4] = {" + ",".join(jpg_entries) + "};")
    out.write_text("\n".join(arrays) + "\n")
    print("Generated photographic PNG/JPEG room assets:", out)
