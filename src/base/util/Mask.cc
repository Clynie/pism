// Copyright (C) 2011, 2014, 2015 Constantine Khroulev and David Maxwell
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include <cassert>

#include "Mask.hh"
#include "IceGrid.hh"

namespace pism {

void GeometryCalculator::compute(IceModelVec2S &bed, IceModelVec2S &thickness,
                                 IceModelVec2Int &out_mask, IceModelVec2S &out_surface)
{
  IceModelVec::AccessList list;
  list.add(bed);
  list.add(thickness);
  list.add(out_mask);
  list.add(out_surface);
  const IceGrid &grid = *bed.get_grid();
  
  unsigned int stencil = out_mask.get_stencil_width();
  assert(bed.get_stencil_width()         >= stencil);
  assert(thickness.get_stencil_width()   >= stencil);
  assert(out_surface.get_stencil_width() >= stencil);

  for (PointsWithGhosts p(grid, stencil); p; p.next()) {
    const int i = p.i(), j = p.j();

    int mask_value;
    compute(bed(i,j),thickness(i,j),&mask_value,&out_surface(i,j));
    out_mask(i,j) = mask_value;
  }
  
}

} // end of namespace pism
