# GOOZ: Header-Only G-code Oozing Pattern Generator (Alpha)

**GOOZ** is an experimental (alpha), **header-only** G-code generator library designed to create interesting 3D printer patterns by **maximizing controlled oozing**. By adding geometric shapes (cubes, cylinders) directly via a single `gooz.h` include, Gooz produces unique infill and perimeter structures that emphasize ooze-based textures and artistic effects.

> **Warning:** This is a very early alpha release. The API and output may change in future versions. Use at your own risk and always verify generated G-code in a 3D printer simulator before printing.

![image](https://github.com/user-attachments/assets/15309cff-d2c3-4387-af35-cdfbb09f396c)

---

## Features

* **Header-Only**: Just include `gooz.h` in your C/C++ project—no separate library to build.
* **Shape Primitives**: Add cubes or cylinders at any X/Y location.
* **Oozing-Driven Extrusion**: Custom G-code moves that intentionally ooze filament between points, creating distinctive patterns.
* **Configurable Settings**: Easily set nozzle diameter, filament diameter, layer height, print/travel speeds, oozing ratios, and more.
* **Build Plate Checks**: Prevents shapes from exceeding build volume limits.
* **Single-Extruder Support**: Generates standard G-code compatible with printers like the Creality Ender 3 V2.

## Getting Started

1. **Clone the repository**:

   ```bash
   git clone https://github.com/yourusername/gooz.git
   cd gooz
   ```

2. **Include in your project**:

   Simply copy `gooz.h` into your source tree and define `GOOZ_IMPLEMENTATION` in one C file:

   ```c
   #define GOOZ_IMPLEMENTATION
   #include "gooz.h"
   ```

3. **Compile**:

   ```bash
   gcc main.c -o gooz -lm
   ```

## Usage Example

```c
#define GOOZ_IMPLEMENTATION
#include "gooz.h"

int main(void) {
    // 1. Set printer & material properties
    GoozPrintSettings settings = {
        .nozzle_diameter   = 0.4,
        .filament_diameter = 1.75,
        .layer_height      = 0.2,
        .print_speed       = 1800,
        .travel_speed      = 6000,
        .print_width_ratio = 1.1,
        .oozing_ratio      = 0.5,
        .oozing_z_security = 1.0,
        .temp_nozzle       = 220,
        .temp_bed          = 60,
        .build_width       = 220,
        .build_depth       = 220,
        .build_height      = 250
    };
    gooz_set_print_settings(&settings);

    // 2. Add a circle of small cubes centered at (80,80)
    const float cx = 80.0f, cy = 80.0f, r = 50.0f;
    const int nbr_cubes=36;
    for(int i = 0; i < nbr_cubes; ++i) {
        float theta = 2.0f * M_PI * i / nbr_cubes;    // divide circle into 10 segments
        float x = cx + r * cos(theta);
        float y = cy + r * sin(theta);
        gooz_add_cube(x, y, 1.5f);
    }

    // 3. Generate the G-code file
    FILE *f = fopen("output.gcode", "w");
    gooz_generate_gcode(f);
    fclose(f);

    gooz_free();
    return 0;
}
```

This example produces a small grid of cubes with oozing extrusions between perimeter and infill.

## Alpha Status & Roadmap

* ✅ **Header-only support**
* ✅ **Basic shape support** (cubes, cylinders)
* ✅ **Oozing extrusion moves**
* ⚠️ **Alpha**: expect API changes and bugs.
* 🔜 **Planned**: Add more shapes, multi-material support, slicer plugin integration, interactive preview.

## Contributing

Contributions are welcome! Please open issues or submit pull requests on the GitHub repository. Ensure all new code is documented and tested.

## License

This project is released under the **MIT License**. See `LICENSE` for details.
