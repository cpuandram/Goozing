#define GOOZ_IMPLEMENTATION
#include "gooz.h"

int main(void) {
    // 1. Set printer & material properties
    GoozPrintSettings settings = {
        .nozzle_diameter   = 0.4,
        .filament_diameter = 1.75,
        .layer_height      = 0.2,
        .print_speed       = 1500,
        .travel_speed      = 6000,
        .print_width_ratio = 1.1,
        .oozing_ratio      = 0.3,
        .oozing_z_security = 4.0,
        .temp_nozzle       = 217,
        .temp_bed          = 60,
        .build_width       = 220,
        .build_depth       = 220,
        .build_height      = 250
    };

    gooz_set_print_settings(&settings);
    for (double i=10; i<220; i+=22.0){
      for (double j=10; j<220; j+=22.0){
	gooz_add_cube(i,j,5+((double)(rand()%100))/50.0);
      }
    }

    // 3. Generate the G-code file
    FILE *f = fopen("output.gcode", "w");
    gooz_generate_gcode(f);
    fclose(f);

    gooz_free();
    return 0;
}
