#include "openmc/geometry.h"

#include <array>
#include <sstream>

#include "openmc/cell.h"
#include "openmc/constants.h"
#include "openmc/error.h"
#include "openmc/lattice.h"
#include "openmc/settings.h"
#include "openmc/simulation.h"
#include "openmc/surface.h"


namespace openmc {

//==============================================================================
// Global variables
//==============================================================================


namespace model {

int root_universe {-1};

std::vector<int64_t> overlap_check_count;

} // namespace model

//==============================================================================
// Non-member functions
//==============================================================================

extern "C" bool
check_cell_overlap(Particle* p)
{
  int n_coord = p->n_coord;

  // Loop through each coordinate level
  for (int j = 0; j < n_coord; j++) {
    Universe& univ = *model::universes[p->coord[j].universe];
    int n = univ.cells_.size();

    // Loop through each cell on this level
    for (auto index_cell : univ.cells_) {
      Cell& c = *model::cells[index_cell];
      if (c.contains(p->coord[j].xyz, p->coord[j].uvw, p->surface)) {
        if (index_cell != p->coord[j].cell) {
          std::stringstream err_msg;
          err_msg << "Overlapping cells detected: " << c.id_ << ", "
                  << model::cells[p->coord[j].cell]->id_ << " on universe "
                  << univ.id_;
          fatal_error(err_msg);
        }
        ++model::overlap_check_count[index_cell];
      }
    }
  }

  return false;
}

//==============================================================================

bool
find_cell_inner(Particle* p, const NeighborList* neighbor_list)
{
  // Find which cell of this universe the particle is in.  Use the neighbor list
  // to shorten the search if one was provided.
  bool found = false;
  int32_t i_cell;
  if (neighbor_list) {
    for (auto it = neighbor_list->cbegin(); it != neighbor_list->cend(); ++it) {
      i_cell = *it;

      // Make sure the search cell is in the same universe.
      int i_universe = p->coord[p->n_coord-1].universe;
      if (model::cells[i_cell]->universe_ != i_universe) continue;

      // Check if this cell contains the particle.
      Position r {p->coord[p->n_coord-1].xyz};
      Direction u {p->coord[p->n_coord-1].uvw};
      auto surf = p->surface;
      if (model::cells[i_cell]->contains(r, u, surf)) {
        p->coord[p->n_coord-1].cell = i_cell;
        found = true;
        break;
      }
    }

  } else {
    int i_universe = p->coord[p->n_coord-1].universe;
    const auto& cells {model::universes[i_universe]->cells_};
    for (auto it = cells.cbegin(); it != cells.cend(); it++) {
      i_cell = *it;

      // Make sure the search cell is in the same universe.
      int i_universe = p->coord[p->n_coord-1].universe;
      if (model::cells[i_cell]->universe_ != i_universe) continue;

      // Check if this cell contains the particle.
      Position r {p->coord[p->n_coord-1].xyz};
      Direction u {p->coord[p->n_coord-1].uvw};
      auto surf = p->surface;
      if (model::cells[i_cell]->contains(r, u, surf)) {
        p->coord[p->n_coord-1].cell = i_cell;
        found = true;
        break;
      }
    }
  }

  // Announce the cell that the particle is entering.
  if (found && (settings::verbosity >= 10 || simulation::trace)) {
    std::stringstream msg;
    msg << "    Entering cell " << model::cells[i_cell]->id_;
    write_message(msg, 1);
  }

  if (found) {
    Cell& c {*model::cells[i_cell]};
    if (c.type_ == FILL_MATERIAL) {
      //=======================================================================
      //! Found a material cell which means this is the lowest coord level.

      // Find the distribcell instance number.
      if (c.material_.size() > 1 || c.sqrtkT_.size() > 1) {
        int offset = 0;
        for (int i = 0; i < p->n_coord; i++) {
          Cell& c_i {*model::cells[p->coord[i].cell]};
          if (c_i.type_ == FILL_UNIVERSE) {
            offset += c_i.offset_[c.distribcell_index_];
          } else if (c_i.type_ == FILL_LATTICE) {
            Lattice& lat {*model::lattices[p->coord[i+1].lattice-1]};
            int i_xyz[3] {p->coord[i+1].lattice_x,
                          p->coord[i+1].lattice_y,
                          p->coord[i+1].lattice_z};
            if (lat.are_valid_indices(i_xyz)) {
              offset += lat.offset(c.distribcell_index_, i_xyz);
            }
          }
        }
        p->cell_instance = offset;
      } else {
        p->cell_instance = 0;
      }

      // Set the material and temperature.
      p->last_material = p->material;
      int32_t mat;
      if (c.material_.size() > 1) {
        mat = c.material_[p->cell_instance];
      } else {
        mat = c.material_[0];
      }
      if (mat == MATERIAL_VOID) {
        p->material = MATERIAL_VOID;
      } else {
        p->material = mat + 1;
      }
      p->last_sqrtkT = p->sqrtkT;
      if (c.sqrtkT_.size() > 1) {
        p->sqrtkT = c.sqrtkT_[p->cell_instance];
      } else {
        p->sqrtkT = c.sqrtkT_[0];
      }

      return true;

    } else if (c.type_ == FILL_UNIVERSE) {
      //========================================================================
      //! Found a lower universe, update this coord level then search the next.

      // Set the lower coordinate level universe.
      p->coord[p->n_coord].universe = c.fill_;

      // Set the position and direction.
      for (int i = 0; i < 3; i++) {
        p->coord[p->n_coord].xyz[i] = p->coord[p->n_coord-1].xyz[i];
        p->coord[p->n_coord].uvw[i] = p->coord[p->n_coord-1].uvw[i];
      }

      // Apply translation.
      p->coord[p->n_coord].xyz[0] -= c.translation_.x;
      p->coord[p->n_coord].xyz[1] -= c.translation_.y;
      p->coord[p->n_coord].xyz[2] -= c.translation_.z;

      // Apply rotation.
      if (!c.rotation_.empty()) {
        auto x = p->coord[p->n_coord].xyz[0];
        auto y = p->coord[p->n_coord].xyz[1];
        auto z = p->coord[p->n_coord].xyz[2];
        p->coord[p->n_coord].xyz[0] = x*c.rotation_[3] + y*c.rotation_[4]
                                      + z*c.rotation_[5];
        p->coord[p->n_coord].xyz[1] = x*c.rotation_[6] + y*c.rotation_[7]
                                      + z*c.rotation_[8];
        p->coord[p->n_coord].xyz[2] = x*c.rotation_[9] + y*c.rotation_[10]
                                      + z*c.rotation_[11];
        auto u = p->coord[p->n_coord].uvw[0];
        auto v = p->coord[p->n_coord].uvw[1];
        auto w = p->coord[p->n_coord].uvw[2];
        p->coord[p->n_coord].uvw[0] = u*c.rotation_[3] + v*c.rotation_[4]
                                      + w*c.rotation_[5];
        p->coord[p->n_coord].uvw[1] = u*c.rotation_[6] + v*c.rotation_[7]
                                      + w*c.rotation_[8];
        p->coord[p->n_coord].uvw[2] = u*c.rotation_[9] + v*c.rotation_[10]
                                      + w*c.rotation_[11];
        p->coord[p->n_coord].rotated = true;
      }

      // Update the coordinate level and recurse.
      ++p->n_coord;
      return find_cell_inner(p, nullptr);

    } else if (c.type_ == FILL_LATTICE) {
      //========================================================================
      //! Found a lower lattice, update this coord level then search the next.

      Lattice& lat {*model::lattices[c.fill_]};

      // Determine lattice indices.
      Position r {p->coord[p->n_coord-1].xyz};
      Direction u {p->coord[p->n_coord-1].uvw};
      r += TINY_BIT * u;
      auto i_xyz = lat.get_indices(r);

      // Store lower level coordinates.
      r = lat.get_local_position(p->coord[p->n_coord-1].xyz, i_xyz);
      p->coord[p->n_coord].xyz[0] = r.x;
      p->coord[p->n_coord].xyz[1] = r.y;
      p->coord[p->n_coord].xyz[2] = r.z;
      p->coord[p->n_coord].uvw[0] = u.x;
      p->coord[p->n_coord].uvw[1] = u.y;
      p->coord[p->n_coord].uvw[2] = u.z;

      // Set lattice indices.
      p->coord[p->n_coord].lattice = c.fill_ + 1;
      p->coord[p->n_coord].lattice_x = i_xyz[0];
      p->coord[p->n_coord].lattice_y = i_xyz[1];
      p->coord[p->n_coord].lattice_z = i_xyz[2];

      // Set the lower coordinate level universe.
      if (lat.are_valid_indices(i_xyz)) {
        p->coord[p->n_coord].universe = lat[i_xyz];
      } else {
        if (lat.outer_ != NO_OUTER_UNIVERSE) {
          p->coord[p->n_coord].universe = lat.outer_;
        } else {
          std::stringstream err_msg;
          err_msg << "Particle " << p->id << " is outside lattice "
                  << lat.id_ << " but the lattice has no defined outer "
                  "universe.";
          warning(err_msg);
          return false;
        }
      }

      // Update the coordinate level and recurse.
      ++p->n_coord;
      return find_cell_inner(p, nullptr);
    }
  }

  return found;
}

//==============================================================================

extern "C" bool
find_cell(Particle* p, bool use_neighbor_lists)
{
  // Determine universe (if not yet set, use root universe).
  int i_universe = p->coord[p->n_coord-1].universe;
  if (i_universe == C_NONE) {
    p->coord[0].universe = model::root_universe;
    p->n_coord = 1;
    i_universe = model::root_universe;
  }

  // Reset all the deeper coordinate levels.
  for (int i = p->n_coord; i < MAX_COORD; i++) {
    p->coord[i].reset();
  }

  if (use_neighbor_lists) {
    // Get the cell this particle was in previously.
    auto coord_lvl = p->n_coord - 1;
    auto i_cell = p->coord[coord_lvl].cell;
    Cell& c {*model::cells[i_cell]};

    // Search for the particle in that cell's neighbor list.  Return if we
    // found the particle.
    bool found = find_cell_inner(p, &c.neighbors_);
    if (found) return found;

    // The particle could not be found in the neighbor list.  Try searching all
    // cells in this universe, and update the neighbor list if we find a new
    // neighboring cell.
    found = find_cell_inner(p, nullptr);
    if (found) c.neighbors_.push_back(p->coord[coord_lvl].cell);
    return found;

  } else {
    // Search all cells in this universe for the particle.
    return find_cell_inner(p, nullptr);
  }
}

//==============================================================================

extern "C" void
cross_lattice(Particle* p, int lattice_translation[3])
{
  Lattice& lat {*model::lattices[p->coord[p->n_coord-1].lattice-1]};

  if (settings::verbosity >= 10 || simulation::trace) {
    std::stringstream msg;
    msg << "    Crossing lattice " << lat.id_ << ". Current position ("
         << p->coord[p->n_coord-1].lattice_x << ","
         << p->coord[p->n_coord-1].lattice_y << ","
         << p->coord[p->n_coord-1].lattice_z << ")";
    write_message(msg, 1);
  }

  // Set the lattice indices.
  p->coord[p->n_coord-1].lattice_x += lattice_translation[0];
  p->coord[p->n_coord-1].lattice_y += lattice_translation[1];
  p->coord[p->n_coord-1].lattice_z += lattice_translation[2];
  std::array<int, 3> i_xyz {p->coord[p->n_coord-1].lattice_x,
                            p->coord[p->n_coord-1].lattice_y,
                            p->coord[p->n_coord-1].lattice_z};

  // Set the new coordinate position.
  auto r = lat.get_local_position(p->coord[p->n_coord-2].xyz, i_xyz);
  p->coord[p->n_coord-1].xyz[0] = r.x;
  p->coord[p->n_coord-1].xyz[1] = r.y;
  p->coord[p->n_coord-1].xyz[2] = r.z;

  if (!lat.are_valid_indices(i_xyz)) {
    // The particle is outside the lattice.  Search for it from the base coords.
    p->n_coord = 1;
    bool found = find_cell(p, 0);
    if (!found && p->alive) {
      std::stringstream err_msg;
      err_msg << "Could not locate particle " << p->id
              << " after crossing a lattice boundary";
      p->mark_as_lost(err_msg);
    }

  } else {
    // Find cell in next lattice element.
    p->coord[p->n_coord-1].universe = lat[i_xyz];
    bool found = find_cell(p, 0);

    if (!found) {
      // A particle crossing the corner of a lattice tile may not be found.  In
      // this case, search for it from the base coords.
      p->n_coord = 1;
      bool found = find_cell(p, 0);
      if (!found && p->alive) {
        std::stringstream err_msg;
        err_msg << "Could not locate particle " << p->id
                << " after crossing a lattice boundary";
        p->mark_as_lost(err_msg);
      }
    }
  }
}

//==============================================================================

extern "C" void
distance_to_boundary(Particle* p, double* dist, int* surface_crossed,
                     int lattice_translation[3], int* next_level)
{
  *dist = INFINITY;
  double d_lat = INFINITY;
  double d_surf = INFINITY;
  lattice_translation[0] = 0;
  lattice_translation[1] = 0;
  lattice_translation[2] = 0;
  int32_t level_surf_cross;
  std::array<int, 3> level_lat_trans;

  // Loop over each coordinate level.
  for (int i = 0; i < p->n_coord; i++) {
    Position r {p->coord[i].xyz};
    Direction u {p->coord[i].uvw};
    Cell& c {*model::cells[p->coord[i].cell]};

    // Find the oncoming surface in this cell and the distance to it.
    auto surface_distance = c.distance(r, u, p->surface);
    d_surf = surface_distance.first;
    level_surf_cross = surface_distance.second;

    // Find the distance to the next lattice tile crossing.
    if (p->coord[i].lattice != F90_NONE) {
      Lattice& lat {*model::lattices[p->coord[i].lattice-1]};
      std::array<int, 3> i_xyz {p->coord[i].lattice_x, p->coord[i].lattice_y,
                                p->coord[i].lattice_z};
      //TODO: refactor so both lattice use the same position argument (which
      //also means the lat.type attribute can be removed)
      std::pair<double, std::array<int, 3>> lattice_distance;
      switch (lat.type_) {
        case LatticeType::rect:
          lattice_distance = lat.distance(r, u, i_xyz);
          break;
        case LatticeType::hex:
          Position r_hex {p->coord[i-1].xyz[0], p->coord[i-1].xyz[1],
                          p->coord[i].xyz[2]};
          lattice_distance = lat.distance(r_hex, u, i_xyz);
          break;
      }
      d_lat = lattice_distance.first;
      level_lat_trans = lattice_distance.second;

      if (d_lat < 0) {
        std::stringstream err_msg;
        err_msg << "Particle " << p->id
                << " had a negative distance to a lattice boundary";
        p->mark_as_lost(err_msg);
      }
    }

    // If the boundary on this coordinate level is coincident with a boundary on
    // a higher level then we need to make sure that the higher level boundary
    // is selected.  This logic must consider floating point precision.
    if (d_surf < d_lat) {
      if (*dist == INFINITY || ((*dist) - d_surf)/(*dist) >= FP_REL_PRECISION) {
        *dist = d_surf;

        // If the cell is not simple, it is possible that both the negative and
        // positive half-space were given in the region specification. Thus, we
        // have to explicitly check which half-space the particle would be
        // traveling into if the surface is crossed
        if (c.simple_) {
          *surface_crossed = level_surf_cross;
        } else {
          Position r_hit = r + d_surf * u;
          Surface& surf {*model::surfaces[std::abs(level_surf_cross)-1]};
          Direction norm = surf.normal(r_hit);
          if (u.dot(norm) > 0) {
            *surface_crossed = std::abs(level_surf_cross);
          } else {
            *surface_crossed = -std::abs(level_surf_cross);
          }
        }

        lattice_translation[0] = 0;
        lattice_translation[1] = 0;
        lattice_translation[2] = 0;
        *next_level = i + 1;
      }
    } else {
      if (*dist == INFINITY || ((*dist) - d_lat)/(*dist) >= FP_REL_PRECISION) {
        *dist = d_lat;
        *surface_crossed = F90_NONE;
        lattice_translation[0] = level_lat_trans[0];
        lattice_translation[1] = level_lat_trans[1];
        lattice_translation[2] = level_lat_trans[2];
        *next_level = i + 1;
      }
    }
  }
}

} // namespace openmc
