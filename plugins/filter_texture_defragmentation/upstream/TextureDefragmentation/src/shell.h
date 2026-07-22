/*******************************************************************************
    Copyright (c) 2021, Andrea Maggiordomo, Paolo Cignoni and Marco Tarini

    This file is part of TextureDefrag, a reference implementation for
    the paper ``Texture Defragmentation for Photo-Reconstructed 3D Models''
    by Andrea Maggiordomo, Paolo Cignoni and Marco Tarini.

    TextureDefrag is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    TextureDefrag is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with TextureDefrag. If not, see <https://www.gnu.org/licenses/>.
*******************************************************************************/

#ifndef SHELL_H
#define SHELL_H

class Mesh;
class FaceGroup;

class Mesh;
class FaceGroup;

/* Builds a shell for the given chart. A shell is a mesh object specifically
 * constructed to compute the parameterization of a chart. In order to support
 * the various operations that we need to perform on it, it is more convenient
 * to keep its shape as the current 2D parameter-space configuration (possibly
 * not updated). The shell has suitable attributes to retrieve information about
 * the shell-face to input mesh-face mappings, as well as the target shape
 * features of each face to guide the parameterization process. (See also the
 * comments in mesh_attribute.h). */

/*!
 * Computes for each face in the merged chart their target UV shape.
 * The targets are stored in the shell mesh and will be used by the
 * ARAP optimizer.
 *
 * @param shell:
 * @param fg:
 * @param downscaleFactor:
 *
 * @return if the computed shell mesh is a single connected component or not.
 */
bool BuildShellWithTargetsFromUV(Mesh& shell, FaceGroup& fg, double downscaleFactor);

/*!
 * Given a shell mesh, it updates it by filling all its inner holes through a
 * ear-cutting triangulation algorithm procedure. All added faces are marked
 * as `HOLE_FILLING`, to distinguish them from the original ones.
 *
 * @param shell: the mesh instance.
 */
void CloseHoles3D(Mesh& shell);

/* This function synchronizes a shell with its UV coordinates, that is it
 * updates its vertex coordinates to match the parameter space configurations
 * (with z = 0). The operation is performed per-vertex. */
/*!
 * Updates a shell mesh, repositioning all its 3D vertex coordinates according
 * to their UV parametrization positions. Note that, since each parametrization
 * point is bidimensional (only consider the x and y axis), while a tridimensional
 * point has the extra z-axis, we always set this last one to zero.
 *
 * @param shell: the shell mesh instance.
 */
void SyncShellWithUV(Mesh& shell);

/* This function synchronizes a shell with the model space coordinates of its
 * chart. */
void SyncShellWith3D(Mesh& shell);

/* Removes any hole-filling face from the shell and compacts its containers */
void ClearHoleFillingFaces(Mesh& shell, bool holefill, bool scaffold);

#endif // SHELL_H
