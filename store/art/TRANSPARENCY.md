# Transparent interface graphics

The wordmark and Mission Stick cutouts were edited with the built-in `image_gen` tool. The other six hardware graphics reuse their existing transparent masters unchanged. `tool/make_saturn_art.py --interface-only` copies these PNGs into `frontend/assets` without flattening their alpha; the app trims transparent padding and sizes textures at load time.

## wordmark

Saved master: [wordmark-transparent.png](wordmark-transparent.png)

Final prompt:

> Use case: background-extraction. Edit target: the supplied Retro-Saturn wordmark image used in an existing app. Remove ONLY the dark rectangular blue-black gradient backdrop and make it genuinely transparent. Preserve the exact existing logo: the blue circular Saturn swirl on the left, the red-orange-yellow script word "Retro" above the blue uppercase word "SATURN" on the right. Keep the exact lettering, spelling, arrangement, proportions, colours and original graphic details unchanged. Keep the entire mark in frame and its original wide composition, with minimal transparent padding. No redesigned logo, no additional objects, no checkerboard painted into the pixels. Output PNG with real alpha transparency.

## Mission Stick

Saved master: [mission-stick-transparent.png](mission-stick-transparent.png)

Final prompt:

> Use case: background-extraction. Edit target: the supplied Sega Saturn Mission Stick controller graphic. Remove ONLY the light grey-and-white checkerboard backdrop surrounding the product, including the large gap between the two handles, and replace it with real alpha transparency. Preserve the controller itself exactly: both black handles, the left red button and right red trigger, the black base, blue accents, all switches, arrows, white logos and lettering, including SEGA SATURN and MISSION STICK. Keep the exact shape, viewpoint, composition, proportions, lighting and colours unchanged. Keep the whole product in frame. No invented shadows or background, no redesign, no additional elements, no painted checkerboard. Output PNG with a genuinely transparent background.
