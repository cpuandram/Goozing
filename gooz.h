#ifndef GOOZ_H
#define GOOZ_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Public API

// Printer/material and build plate settings
typedef struct {
  double nozzle_diameter;   // e.g. 0.4
  double filament_diameter; // e.g. 1.75
  double layer_height;      // e.g. 0.2
  int print_speed;       // mm/min
  int travel_speed;      // mm/min
  double print_width_ratio;
  double oozing_ratio;
  double oozing_z_security;
  int temp_nozzle;     // °C
  int temp_bed;        // °C
  double build_width;  // Max X (in mm)
  double build_depth;  // Max Y (in mm)
  double build_height; // Max Z (in mm)
} GoozPrintSettings;

// Set up your material/printer parameters before generating G-code.
void gooz_set_print_settings(const GoozPrintSettings *settings);

int gooz_add_cube(double x, double y, double size);
int gooz_add_cylinder(double x, double y, double radius, double height);

// Generate the G-code file.
void gooz_generate_gcode(FILE *out);
void gooz_free();

#ifdef GOOZ_IMPLEMENTATION

// Constants
static const double GOOZ_TINY_VALUE = 5e-4;
static const double GOOZ_OFFSET_Z = 0.1;
// Internal Structures
typedef enum { FORM_CUBE, FORM_CYLINDER } GoozFormType;

typedef struct {
  double x, y;
  GoozFormType type;
  union {
    struct {
      double side;
    } cube;
    struct {
      double radius, height;
    } cylinder;
  } shape;
  struct {
    size_t idx_key;
    size_t cur_layer;
    size_t tot_layer;
    double height_layer;
  } state;
} GoozForm;

typedef struct {
  GoozForm *forms;
  size_t nbr_forms;
  size_t capacity;

} GoozState;

typedef struct {
  double x, y, z, e;
  int layer;
} GoozNozzleState;

static GoozState state = {NULL, 0, 0};
static GoozPrintSettings print_settings;
static GoozNozzleState nozzle_state = {0, 0, 0, 0, 0};

static int ensure_capacity(size_t extra) {
  if (state.nbr_forms + extra > state.capacity) {
    size_t new_cap = (state.capacity == 0) ? 8 : state.capacity * 2;
    GoozForm *new_forms = realloc(state.forms, new_cap * sizeof(GoozForm));
    if (!new_forms)
      return -1;
    state.forms = new_forms;
    state.capacity = new_cap;
  }
  return 0;
}

static int check_within_build(double x, double y, double w, double h) {
  // must be non-negative and within plate
  if (w < 0 || h < 0)
    return 0;
  if (x - w < 0 || y - w < 0)
    return 0;
  if (x + w > print_settings.build_width || y + w > print_settings.build_depth)
    return 0;
  if (h > print_settings.build_height)
    return 0;
  return 1;
}

static size_t compute_nbr_pass(double total_size, double ideal_size) {
  if (total_size <= 0.0 || ideal_size <= 0.0) {
    return 1;
  }
  double ideal_count = total_size / ideal_size;
  double f = floor(ideal_count);
  if (f < 1.0) {
    return 1;
  }
  double err_down = total_size / f - ideal_size;
  double err_up = ideal_size - total_size / (f + 1.0);
  return (err_down < err_up) ? (size_t)f : (size_t)(f + 1.0);
}

int gooz_add_cube(double x, double y, double size) {
  if (!check_within_build(x, y, size / 2, size))
    return -1;
  if (ensure_capacity(1) != 0)
    return -2;
  size_t nbr_layers = compute_nbr_pass(size, print_settings.layer_height);
  GoozForm f = {x,
                y,
                FORM_CUBE,
                .shape.cube.side = size,
                .state.idx_key = state.nbr_forms,
                .state.cur_layer = 0,
                .state.tot_layer = nbr_layers,
                .state.height_layer = size / (double)nbr_layers};
  state.forms[state.nbr_forms++] = f;
  return 0;
}

int gooz_add_cylinder(double x, double y, double radius, double height) {
  if (!check_within_build(x, y, radius, height))
    return -1;
  if (ensure_capacity(1) != 0)
    return -2;
  size_t nbr_layers = compute_nbr_pass(height, print_settings.layer_height);
  GoozForm f = {x,
                y,
                FORM_CYLINDER,
                .shape.cylinder.radius = radius,
                .shape.cylinder.height = height,
                .state.idx_key = state.nbr_forms,
                .state.cur_layer = 0,
                .state.tot_layer = nbr_layers,
                .state.height_layer = height / (double)nbr_layers};
  state.forms[state.nbr_forms++] = f;
  return 0;
}

void gooz_free() {
  free(state.forms);
  state.forms = NULL;
  state.nbr_forms = state.capacity = 0;
}

static double extrusion_length(double area_mm2, double length_mm) {
  double r = print_settings.filament_diameter / 2.0;
  double filament_area = M_PI * r * r;
  return (area_mm2 * length_mm) / filament_area;
}

static void set_nozzle(double x, double y, double z, double e) {
  nozzle_state.x = x;
  nozzle_state.y = y;
  nozzle_state.z = z;
  nozzle_state.e = e;
}

static void add_move(FILE *out, double x, double y, double z) {
  if (z > nozzle_state.z + GOOZ_TINY_VALUE) {
    fprintf(out, "G1 Z%.2f F%d ; Move head up\n", z,
            print_settings.travel_speed);
    fprintf(out, "G1 X%.2f Y%.2f F%d ; Move head in XY plane\n", x, y,
            print_settings.travel_speed);
  } else if (z < nozzle_state.z - GOOZ_TINY_VALUE) {
    fprintf(out, "G1 X%.2f Y%.2f F%d ; Move head in XY plane\n", x, y,
            print_settings.travel_speed);
    fprintf(out, "G1 Z%.2f F%d ; Move head down\n", z,
            print_settings.travel_speed);
  } else {
    fprintf(out, "G1 X%.2f Y%.2f F%d ; Move head in XY plane\n", x, y,
            print_settings.travel_speed);
  }
  set_nozzle(x, y, z, nozzle_state.e);
}

static void add_move_oozing(FILE *out, double x, double y, double z) {
  fprintf(out, "; add_line_oozing\n");

  double area = print_settings.nozzle_diameter * print_settings.layer_height * print_settings.print_width_ratio;
  double dx = fabs(x - nozzle_state.x);
  double dy = fabs(y - nozzle_state.y);
  double dz = fabs(z - nozzle_state.z);
  double dist = sqrt(dx * dx + dy * dy + dz * dz);
  double e = extrusion_length(area, dist * print_settings.oozing_ratio);

  fprintf(out, "G92 E0 ; Reset extrusion head\n");
  fprintf(out, "G1 Z%.2f E%.2f F%d ; Add oozing up\n",
          z + print_settings.oozing_z_security,
          e * (dz + print_settings.oozing_z_security) /
              (dx + dy + dz + print_settings.oozing_z_security),
          print_settings.travel_speed);
  fprintf(out, "G1 X%.2f Y%.2f E%.2f F%d ; Add oozing in XY plane\n",
	  (nozzle_state.x+x)/2, (nozzle_state.y+y)/2,
          e, print_settings.travel_speed);
  fprintf(out, "G1 X%.2f Y%.2f F%d ; Add oozing in XY plane\n\n", x, y,
          print_settings.travel_speed);
  fprintf(out, "G1 Z%.2f F%d ; Remove oozing security\n", z,
          print_settings.travel_speed);
  set_nozzle(x, y, z, e);
}

static void add_line(FILE *out, double x, double y, double z, double width) {
  double area =
      width * print_settings.layer_height * print_settings.print_width_ratio;
  double dx = fabs(x - nozzle_state.x);
  double dy = fabs(y - nozzle_state.y);
  double dz = fabs(z - nozzle_state.z);
  double dist = sqrt(dx * dx + dy * dy + dz * dz);
  double e = extrusion_length(area, dist);

  fprintf(out, "G1 X%.2f Y%.2f Z%.2f E%.2f F%d ; Add line\n", x, y, z,
          nozzle_state.e + e, print_settings.print_speed);

  set_nozzle(x, y, z, nozzle_state.e + e);
}

static void emit_startup(FILE *out) {
  fprintf(out, "; START G-code\n");
  fprintf(out, "G90 ; use absolute coordinates\n");
  fprintf(out, "M140 S%d ; Bed temp\n", print_settings.temp_bed);
  fprintf(out, "M104 S%d ; Nozzle temp\n", print_settings.temp_nozzle);
  fprintf(out, "M190 S%d ; Wait bed\n", print_settings.temp_bed);
  fprintf(out, "M109 S%d ; Wait nozzle\n", print_settings.temp_nozzle);
  fprintf(out, "G28 ; Home axes\n");
  fprintf(out, "G92 X0 Y0 Z0 E0 ; Reset coordinates\n\n");

  add_move(out, 0, 0, print_settings.layer_height);
  add_line(out, 100, 0, print_settings.layer_height,
           print_settings.nozzle_diameter);
  add_line(out, 0, 0, print_settings.layer_height,
           print_settings.nozzle_diameter);
}

static void emit_shutdown(FILE *out) {
  fprintf(out, "; END G-code\n");
  fprintf(out, "M104 S0 ; Nozzle off\n");
  fprintf(out, "M140 S0 ; Bed off\n");
  fprintf(out, "G1 X0 Y200 F6000 ; Park head\n");
  fprintf(out, "M84 ; Disable motors\n");
}

static int closest_coord_idx(const double (*coords)[2], int nbr_coords) {
  int min_idx = 0;
  double dx=coords[min_idx][0] - nozzle_state.x;
  double dy=coords[min_idx][1] - nozzle_state.y;
  double min_dist = dx*dx+dy*dy;
  for (int i = 1; i < nbr_coords; ++i) {
    dx=coords[i][0] - nozzle_state.x;
    dy=coords[i][1] - nozzle_state.y;
    double cur_dist = dx*dx+dy*dy;
    if (min_dist > cur_dist) {
      min_idx = i;
      min_dist = cur_dist;
    }
  }
  return min_idx;
}

static void add_cube_perimeter(FILE *out, GoozForm *f) {
  double z = f->state.cur_layer * f->state.height_layer-GOOZ_OFFSET_Z;
  double area = print_settings.nozzle_diameter * print_settings.layer_height;
  double x = f->x, y = f->y,
         s = f->shape.cube.side / 2 -
             print_settings.nozzle_diameter /
                 2; // take into account the nozzle width
  double coords[4][2] = {
      {x - s, y - s}, {x - s, y + s}, {x + s, y + s}, {x + s, y - s}};
  int min_idx = closest_coord_idx(coords, 4);
  add_move_oozing(out, coords[min_idx][0], coords[min_idx][1], z);

  fprintf(out, "; cube perimeter layer %zu\n", f->state.cur_layer);

  for (int i = 1; i <= 4; i++) {
    add_line(out, coords[(min_idx + i) % 4][0], coords[(min_idx + i) % 4][1], z,
             print_settings.nozzle_diameter);
  }
}


static void add_cube_infill_spiral_outward(FILE *out, GoozForm *f) {
  double z = f->state.cur_layer * f->state.height_layer-GOOZ_OFFSET_Z;
  double x = f->x, y = f->y,
         s = f->shape.cube.side - 2 * print_settings.nozzle_diameter;
  size_t nbr_pass = compute_nbr_pass(s, print_settings.nozzle_diameter);
  double width = s / nbr_pass;
  s = (s - width) / 2;
  if (nbr_pass & 1) {
    if (f->state.cur_layer & 1){
      double k=1;
      for (int i = 1; i < nbr_pass; ++i) {
	add_line(out, nozzle_state.x+width*(i)*k, nozzle_state.y, z, width);
	add_line(out, nozzle_state.x, nozzle_state.y+width*(i)*k, z, width);
	k*=-1;
      }
      add_line(out, nozzle_state.x+width*(nbr_pass-1)*k, nozzle_state.y, z, width);
    }
    else{
       double k=-1;
       for (int i = 1; i < nbr_pass; ++i) {
	 add_line(out, nozzle_state.x+width*(i)*k, nozzle_state.y, z, width);
	 add_line(out, nozzle_state.x, nozzle_state.y+width*(i)*k, z, width);
	 k*=-1;
       }
       add_line(out, nozzle_state.x+width*(nbr_pass-1)*k, nozzle_state.y, z, width);
    }
  }
  else {
    double coords[4][2] = {
      {x - width/2, y - width/2}, {x - width/2, y + width/2}, {x + width/2, y + width/2}, {x + width/2, y - width/2}};
    int min_idx = closest_coord_idx(coords, 4);
    add_move(out, coords[min_idx][0], coords[min_idx][1], z);
    double k=1;
    switch (min_idx) {
    case 0:
      for (int i = 1; i < nbr_pass; ++i) {
	add_line(out, nozzle_state.x, nozzle_state.y + width*i*k, z, width);
	add_line(out, nozzle_state.x + width*i*k, nozzle_state.y, z, width);
	k*=-1;
      }
      add_line(out, nozzle_state.x, nozzle_state.y + width*(nbr_pass-1)*k, z, width);
      break;
    case 1:
      for (int i = 1; i < nbr_pass; ++i) {
	add_line(out, nozzle_state.x, nozzle_state.y - width*i*k, z, width);
	add_line(out, nozzle_state.x + width*i*k, nozzle_state.y, z, width);
	k*=-1;
      }
      add_line(out, nozzle_state.x, nozzle_state.y - width*(nbr_pass-1)*k, z, width);
      break;
    case 2:
      for (int i = 1; i < nbr_pass; ++i) {
	add_line(out, nozzle_state.x, nozzle_state.y - width*i*k, z, width);
	add_line(out, nozzle_state.x - width*i*k, nozzle_state.y, z, width);
	k*=-1;
      }
      add_line(out, nozzle_state.x, nozzle_state.y - width*(nbr_pass-1)*k, z, width);
      break;
    case 3:
      for (int i = 1; i < nbr_pass; ++i) {
	add_line(out, nozzle_state.x, nozzle_state.y + width*i*k, z, width);
	add_line(out, nozzle_state.x - width*i*k, nozzle_state.y, z, width);
	k*=-1;
      }
      add_line(out, nozzle_state.x, nozzle_state.y + width*(nbr_pass-1)*k, z, width);
      break;
    default:
      break;
    }
  }
}

static void add_cube_infill_spiral_inward(FILE *out, GoozForm *f) {
  double z = f->state.cur_layer * f->state.height_layer-GOOZ_OFFSET_Z;
  double x = f->x, y = f->y,
         s = f->shape.cube.side - 2 * print_settings.nozzle_diameter;
  size_t nbr_pass = compute_nbr_pass(s, print_settings.nozzle_diameter);
  double width = s / nbr_pass;
  s = (s - width) / 2;
  double coords[4][2] = {
      {x - s, y - s}, {x - s, y + s}, {x + s, y + s}, {x + s, y - s}};
  int min_idx = closest_coord_idx(coords, 4);
  add_move(out, coords[min_idx][0], coords[min_idx][1], z);
  double k=1;
  switch (min_idx) {
  case 0:
    add_line(out, nozzle_state.x, nozzle_state.y + width*(nbr_pass-1)*k, z, width);
    for (int i = nbr_pass-1; i >0; --i) {
      add_line(out, nozzle_state.x + width*i*k, nozzle_state.y, z, width);
      add_line(out, nozzle_state.x, nozzle_state.y - width*i*k, z, width);
      k*=-1;
    }
    break;
  case 1:
    add_line(out, nozzle_state.x + width*(nbr_pass-1)*k, nozzle_state.y, z, width);
    for (int i = nbr_pass-1; i >0; --i) {
      add_line(out, nozzle_state.x, nozzle_state.y - width*i*k, z, width);
      add_line(out, nozzle_state.x - width*i*k, nozzle_state.y, z, width);
      k*=-1;
    }
    break;
  case 2:
    add_line(out, nozzle_state.x, nozzle_state.y - width*(nbr_pass-1)*k, z, width);
    for (int i = nbr_pass-1; i >0; --i) {
      add_line(out, nozzle_state.x - width*i*k, nozzle_state.y, z, width);
      add_line(out, nozzle_state.x, nozzle_state.y + width*i*k, z, width);
      k*=-1;
    }
    break;
  case 3:
    add_line(out, nozzle_state.x - width*(nbr_pass-1)*k, nozzle_state.y, z, width);
    for (int i = nbr_pass-1; i >0; --i) {
      add_line(out, nozzle_state.x, nozzle_state.y + width*i*k, z, width);
      add_line(out, nozzle_state.x + width*i*k, nozzle_state.y, z, width);
      k*=-1;
    }
    break;
  default:
    break;
  }
}

static void add_cube_infill(FILE *out, GoozForm *f) {
  double z = f->state.cur_layer * f->state.height_layer-GOOZ_OFFSET_Z;
  double x = f->x, y = f->y,
         s = f->shape.cube.side - 2 * print_settings.nozzle_diameter;
  size_t nbr_pass = compute_nbr_pass(s, print_settings.nozzle_diameter);
  double width = s / nbr_pass;
  s = (s - width) / 2;
  double coords[4][2] = {
      {x - s, y - s}, {x - s, y + s}, {x + s, y + s}, {x + s, y - s}};
  int min_idx = closest_coord_idx(coords, 4);
  add_move(out, coords[min_idx][0], coords[min_idx][1], z);

  switch (min_idx) {
  case 0:
    for (int i = 0; i < nbr_pass; ++i) {
      if (i & 1) {
        add_line(out, nozzle_state.x, y - s, z, width);
      } else {
        add_line(out, nozzle_state.x, y + s, z, width);
      }
      add_move(out, nozzle_state.x + width, nozzle_state.y, z);
    }
    break;
  case 1:
    for (int i = 0; i < nbr_pass; ++i) {
      if (i & 1) {
        add_line(out, nozzle_state.x, y + s, z, width);
      } else {
        add_line(out, nozzle_state.x, y - s, z, width);
      }
      add_move(out, nozzle_state.x + width, nozzle_state.y, z);
    }
    break;
  case 2:
    for (int i = 0; i < nbr_pass; ++i) {
      if (i & 1) {
        add_line(out, nozzle_state.x, y + s, z, width);
      } else {
        add_line(out, nozzle_state.x, y - s, z, width);
      }
      add_move(out, nozzle_state.x - width, nozzle_state.y, z);
    }
    break;
  case 3:
    for (int i = 0; i < nbr_pass; ++i) {
      if (i & 1) {
        add_line(out, nozzle_state.x, y - s, z, width);
      } else {
        add_line(out, nozzle_state.x, y + s, z, width);
      }
      add_move(out, nozzle_state.x - width, nozzle_state.y, z);
    }
    break;
  default:
    break;
  }
}

static void add_cylinder_perimeter(FILE *out, GoozForm *f) {
  double z = f->state.cur_layer * f->state.height_layer-GOOZ_OFFSET_Z;
  const int seg = 80;
  double step = 2 * M_PI / seg;
  double area = print_settings.nozzle_diameter * print_settings.layer_height;
  double x = f->x, y = f->y, r = f->shape.cylinder.radius;
  double sx = x + r, sy = y;
  fprintf(out, "G1 X%.2f Y%.2f Z%.2f F%d ; cyl layer %zu\n", sx, sy, z,
          print_settings.travel_speed, f->state.cur_layer);
  for (int i = 1; i <= seg; i++) {
    double ang = i * step;
    double nx = x + r * cos(ang), ny = y + r * sin(ang);
    double dist = sqrt((nx - sx) * (nx - sx) + (ny - sy) * (ny - sy));
    double e = extrusion_length(area, dist);
    fprintf(out, "G1 X%.2f Y%.2f E%.5f F%d\n", nx, ny, e,
            print_settings.print_speed);
    sx = nx;
    sy = ny;
  }
  fprintf(out, "G92 E0\n\n");
}

void gooz_set_print_settings(const GoozPrintSettings *s) {
  print_settings = *s;
}

size_t *range(size_t min, size_t max) {
  size_t *range = malloc(sizeof(size_t) * (max - min));
  for (size_t i = 0; i < max-min; ++i) {
    range[i] = i+min;
  }
  return range;
}

GoozForm *get_form_idx(size_t idx) {
  for (size_t i = 0; i < state.nbr_forms; ++i) {
    if (state.forms[i].state.idx_key == idx) {
      return &state.forms[i];
    }
  }
  return NULL;
}

void gooz_generate_gcode(FILE *out) {
  emit_startup(out);

  size_t max_nbr_layers = 0;
  for (size_t i = 0; i < state.nbr_forms; ++i) {
    if (state.forms[i].state.tot_layer > max_nbr_layers) {
      max_nbr_layers = state.forms[i].state.tot_layer;
    }
  }

  size_t *active_idxs = range(0, state.nbr_forms);
  size_t nbr_active_forms = state.nbr_forms;
  for (int l = 1; l <= max_nbr_layers; ++l) {
    for (int i = nbr_active_forms - 1; i >= 0; --i) {
      size_t temp_val = active_idxs[i];
      size_t rand_idx = rand() % (i + 1);
      active_idxs[i] = active_idxs[rand_idx];
      active_idxs[rand_idx] = temp_val;

      GoozForm *cur_form = get_form_idx(active_idxs[i]);
      if (++cur_form->state.cur_layer > cur_form->state.tot_layer) {
        active_idxs[i] = active_idxs[--nbr_active_forms];
        continue;
      }
      double z = cur_form->state.cur_layer * cur_form->state.height_layer;
      switch (cur_form->type) {
	
      case FORM_CUBE:
        add_cube_perimeter(out, cur_form);
        add_cube_infill_spiral_inward(out, cur_form);
        break;

      case FORM_CYLINDER:
        add_cylinder_perimeter(out, cur_form);
        break;

      default:
        break;
      }
    }
  }
  emit_shutdown(out);

  free(active_idxs);
}

#endif // GOOZ_IMPLEMENTATION
#endif // GOOZ_H
