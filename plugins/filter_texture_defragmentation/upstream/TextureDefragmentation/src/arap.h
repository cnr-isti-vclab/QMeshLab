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

#ifndef ARAP_H
#define ARAP_H

#include "mesh.h"

#include <Eigen/Core>
#include <Eigen/Sparse>

struct ARAPSolveInfo {
    double initialEnergy;
    double finalEnergy;
    int iterations;
    bool numericalError;
};

class ARAP {

public:

    struct Cot {
        double v[3];
    };

private:

    Mesh& m;

    std::vector<int> fixed_i;
    std::vector<vcg::Point2d> fixed_pos;

    int max_iter;

    void ComputeSystemMatrix(Mesh& m, const std::vector<Cot>& cotan, Eigen::SparseMatrix<double>& L);
    void ComputeRHS(Mesh& m, const std::vector<Eigen::Matrix2d>& rotations, const std::vector<Cot>& cotan, Eigen::VectorXd& bu, Eigen::VectorXd& bv);

public:

    ARAP(Mesh& mesh);

    double CurrentEnergy();

    /*!
     * Marks a vertex as fixed, meaning it stays pinned in the ARAP optimization
     * solution that will be computed. Note that fixed vertices used by a previous
     * ARAP steps remain marked as such. ARAP keeps track of them in the two static
     * data structures `fixed_i` and `fixed_pos` holding respectively their indices
     * within the mesh and its UV position.
     *
     * @param vp: the vertex pointer.
     * @param pos: the pinned UV position of the vertex.
     */
    void FixVertex(Mesh::ConstVertexPointer vp, const vcg::Point2d& pos);

    void FixBoundaryVertices();

    /*!
     * Determines the amount of fixed vertices (i.e., those that aren't modified
     * by the ARAP procedure). As a side effect, for each of them we store
     * their indices and UV positions in the ARAP data structures `fixed_i` and
     * `fixed_pos`.
     *
     * @return the number of fixed vertices within the input mesh.
     */
    int FixSelectedVertices();

    /*!
     * The ARAP optimization requires at least two fixed vertices within the input
     * mesh; otherwise the optimizer has infinitely many valid solutions to consider.
     *
     * This function fixes two vertices, being the endpoints of an edge whose distance
     * between its original position and target one is within the provided tolerance.
     *
     * @param tol: the tolerance.
     * @return The number of fixed vertices. If the operation is successful, it is
     *         always two, otherwise zero.
     */
    int FixRandomEdgeWithinTolerance(double tol);
    void SetMaxIterations(int n);

    /*!
     * Computes the As-Rigid-As-Possible (ARAP) optimization for the input mesh.
     * The goal is to determine new UV values for each face of the mesh that decrease
     * the ARAP energy.
     *
     * The ARAP energy quantifies the distortion introduced by the merge operation, measuring
     * how much the current transformation mapping from the base UVs to the target UVs differs
     * from a rigid transformation (i.e. one involving only rotations and/or translations).
     *
     * @return: a struct describing the state of the ARAP solution. If the field `numericalError`
     *          is set to true, then the algorithm failed.
     */
    ARAPSolveInfo Solve();

    /*!
     * Computes the global As-Rigid-As-Possible (ARAP) distortion energy. This energy measures how much the transformation
     * for moving from the original UV space to the target UV space differs from a pure rigid transformation (i.e. one
     * requiring only rotations and translations).
     * The energy is computed as the weighted sum of all weighted local energies, divided by the total area. Formally:
     * global energy = ( Σ_{f in Faces} e_f * w_f ) / Σ_{f in Faces} (f.area)
     * where our weights w_f are the local area of face f.
     * Note that the computed numerator and denominator of the global energy are stored in the associated parameters.
     * @param m: the mesh instance
     * @param num: pointer that will be updated with the numerator of the global ARAP energy.
     * @param denom: pointer that will be updated with the denominator of the global ARAP energy.
     * @return the global ARAP energy.
     */
    static double ComputeEnergyFromStoredWedgeTC(Mesh& m, double *num, double *denom);

    /*!
     * Compute the global As-Rigid-As-Possible (ARAP) distortion energy for a limited portion of a mesh (i.e., a set
     * of faces). The computed energy quantifies how much the transportation for moving from the original UV space to the
     * target UV space differs from a pure rigid transformation (i.e., one requiring only rotations and translations).
     *
     * The energy is computed as the weighted sum of all local energies, divided by the total area. Formally:
     * global energy = ( Σ_{f in Faces} e_f * w_f ) / Σ_{f in Faces} (f.area)
     * where our weights w_f are the local area of face f.
     *
     * Note that the computed numerator and denominator of the global energy are stored in the associated parameters.
     *
     * @param fpVec: a vector of faces, usually representing a subset of a mesh.
     * @param m: the mesh instance.
     * @param num: pointer that will be updated with the numerator of the global ARAP energy.
     * @param denom: pointer that will be updated with the denominator of the global ARAP energy.
     * @return the global ARAP energy.
     */
    static double ComputeEnergyFromStoredWedgeTC(const std::vector<Mesh::FacePointer>& fpVec, Mesh& m, double *num, double *denom);
    static double ComputeEnergy(const vcg::Point2d& x10, const vcg::Point2d& x20,
                                const vcg::Point2d& u10, const vcg::Point2d& u20,
                                double *area);
};

#endif // ARAP_H
