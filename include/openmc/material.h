#ifndef OPENMC_MATERIAL_H
#define OPENMC_MATERIAL_H

#include <memory> // for unique_ptr
#include <string>
#include <unordered_map>
#include <vector>

#include <hdf5.h>
#include "pugixml.hpp"
#include "xtensor/xtensor.hpp"

#include "openmc/bremsstrahlung.h"
#include "openmc/particle.h"

namespace openmc {

//==============================================================================
// Global variables
//==============================================================================

class Material;

namespace model {

extern std::vector<Material*> materials;
extern std::unordered_map<int32_t, int32_t> material_map;

} // namespace model

//==============================================================================
//! A substance with constituent nuclides and thermal scattering data
//==============================================================================

class Material
{
public:
  // Types
  struct ThermalTable {
    int index_table; //!< Index of table in data::thermal_scatt
    int index_nuclide; //!< Index in nuclide_
    double fraction; //!< How often to use table
  };

  // Constructors
  Material() {};
  explicit Material(pugi::xml_node material_node);

  // Methods
  void calculate_xs(const Particle& p) const;

  //! Assign thermal scattering tables to specific nuclides within the material
  //! so the code knows when to apply bound thermal scattering data
  void init_thermal();

  //! Set up mapping between global nuclides vector and indices in nuclide_
  void init_nuclide_index();

  //! Finalize the material, assigning tables, normalize density, etc.
  void finalize();

  //! Set total density of the material
  int set_density(double density, std::string units);

  //! Write material data to HDF5
  void to_hdf5(hid_t group) const;

  // Data
  int32_t id_; //!< Unique ID
  std::string name_; //!< Name of material
  std::vector<int> nuclide_; //!< Indices in nuclides vector
  std::vector<int> element_; //!< Indices in elements vector
  xt::xtensor<double, 1> atom_density_; //!< Nuclide atom density in [atom/b-cm]
  double density_; //!< Total atom density in [atom/b-cm]
  double density_gpcc_; //!< Total atom density in [g/cm^3]
  double volume_ {-1.0}; //!< Volume in [cm^3]
  bool fissionable_ {false}; //!< Does this material contain fissionable nuclides
  bool depletable_ {false}; //!< Is the material depletable?
  std::vector<bool> p0_; //!< Indicate which nuclides are to be treated with iso-in-lab scattering

  // To improve performance of tallying, we store an array (direct address
  // table) that indicates for each nuclide in data::nuclides the index of the
  // corresponding nuclide in the nuclide_ vector. If it is not present in the
  // material, the entry is set to -1.
  std::vector<int> mat_nuclide_index_;

  // Thermal scattering tables
  std::vector<ThermalTable> thermal_tables_;

  //! \brief Default temperature for cells containing this material.
  //!
  //! A negative value indicates no default temperature was specified.
  double temperature_ {-1};

  std::unique_ptr<Bremsstrahlung> ttb_;

private:
  //! Initialize bremsstrahlung data
  void init_bremsstrahlung();

  //! Normalize density
  void normalize_density();

  void calculate_neutron_xs(const Particle& p) const;
  void calculate_photon_xs(const Particle& p) const;
};

//==============================================================================
// Fortran compatibility
//==============================================================================

extern "C" int* material_element(int i_material);
extern "C" bool material_isotropic(int i_material, int i_nuc_mat);

} // namespace openmc
#endif // OPENMC_MATERIAL_H
