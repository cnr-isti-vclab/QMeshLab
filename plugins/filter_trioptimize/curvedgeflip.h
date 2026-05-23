/****************************************************************************
 * MeshLab                                                           o o     *
 * A versatile mesh processing toolbox                             o     o   *
 *                                                                _   O  _   *
 * Copyright(C) 2005                                                \/)\/    *
 * Visual Computing Lab                                            /\/|      *
 * ISTI - Italian National Research Council                           |      *
 *                                                                    \      *
 * All rights reserved.                                                      *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License as published by      *
 * the Free Software Foundation; either version 2 of the License, or         *
 * (at your option) any later version.                                       *
 *                                                                           *
 * This program is distributed in the hope that it will be useful,           *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU General Public License (http://www.gnu.org/licenses/gpl.txt)          *
 * for more details.                                                         *
 *                                                                           *
 ****************************************************************************/
#pragma once

#include "curvdata.h"

#include <vcg/complex/algorithms/local_optimization/tri_edge_flip.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/space/triangle3.h>
#include <cmath>
#include <limits>

namespace vcg {
namespace tri {

// Edge flip optimization based on Dyn et al.,
// "Optimizing 3D Triangulations Using Discrete Curvature Analysis".
template <class TRIMESH_TYPE, class MYTYPE, class CURVEVAL>
class CurvEdgeFlip : public TopoEdgeFlip<TRIMESH_TYPE, MYTYPE>
{
protected:
    using FaceType = typename TRIMESH_TYPE::FaceType;
    using FacePointer = typename TRIMESH_TYPE::FacePointer;
    using FaceIterator = typename TRIMESH_TYPE::FaceIterator;
    using VertexIterator = typename TRIMESH_TYPE::VertexIterator;
    using ScalarType = typename TRIMESH_TYPE::ScalarType;
    using VertexPointer = typename TRIMESH_TYPE::VertexPointer;
    using CoordType = typename TRIMESH_TYPE::CoordType;
    using PosType = vcg::face::Pos<FaceType>;
    using VFIteratorType = vcg::face::VFIterator<FaceType>;
    using HeapType = typename LocalOptimization<TRIMESH_TYPE>::HeapType;
    using TriangleType = typename vcg::Triangle3<ScalarType>;

    ScalarType m_cv0 = 0;
    ScalarType m_cv1 = 0;
    ScalarType m_cv2 = 0;
    ScalarType m_cv3 = 0;

    static CurvData FaceCurv(
        VertexPointer v0,
        VertexPointer v1,
        VertexPointer v2,
        CoordType fNormal)
    {
        CurvData res;

        float ang0 = math::Abs(Angle(v1->P() - v0->P(), v2->P() - v0->P()));
        float ang1 = math::Abs(Angle(v0->P() - v1->P(), v2->P() - v1->P()));
        float ang2 = float(M_PI) - ang0 - ang1;

        float s01 = SquaredDistance(v1->P(), v0->P());
        float s02 = SquaredDistance(v2->P(), v0->P());

        if (ang0 >= float(M_PI) / 2.0f) {
            TriangleType triangle(v0->P(), v1->P(), v2->P());
            res.A += (0.5f * DoubleArea(triangle)
                - (s01 * std::tan(ang1) + s02 * std::tan(ang2)) / 8.0f);
        } else if (ang1 >= float(M_PI) / 2.0f) {
            res.A += (s01 * std::tan(ang0)) / 8.0f;
        } else if (ang2 >= float(M_PI) / 2.0f) {
            res.A += (s02 * std::tan(ang0)) / 8.0f;
        } else {
            res.A += ((s02 / std::tan(ang1)) + (s01 / std::tan(ang2))) / 8.0f;
        }

        res.K += ang0;

        ang1 = math::Abs(Angle(fNormal, v1->N()));
        ang2 = math::Abs(Angle(fNormal, v2->N()));
        res.H += (math::Sqrt(s01) / 2.0f) * ang1
            + (math::Sqrt(s02) / 2.0f) * ang2;

        return res;
    }

    static CurvData Curvature(VertexPointer v, FacePointer f1 = nullptr, FacePointer f2 = nullptr)
    {
        CurvData curv;
        VFIteratorType vfi(v);
        while (!vfi.End()) {
            if (vfi.F() != f1 && vfi.F() != f2 && !vfi.F()->IsD()) {
                const int i = vfi.I();
                curv += FaceCurv(
                    vfi.F()->V0(i),
                    vfi.F()->V1(i),
                    vfi.F()->V2(i),
                    vfi.F()->N());
            }
            ++vfi;
        }
        return curv;
    }

public:
    CurvEdgeFlip() = default;

    CurvEdgeFlip(PosType pos, int mark, BaseParameterClass *pp)
    {
        this->_pos = pos;
        this->_localMark = mark;
        this->_priority = ComputePriority(pp);
    }

    CurvEdgeFlip(CurvEdgeFlip &par)
    {
        this->_pos = par.GetPos();
        this->_localMark = par.GetMark();
        this->_priority = par.Priority();
    }

    void Execute(TRIMESH_TYPE &m, BaseParameterClass *) override
    {
        const int i = this->_pos.E();
        FacePointer f1 = this->_pos.F();
        const int j = f1->FFi(i);
        FacePointer f2 = f1->FFp(i);
        VertexPointer v0 = f1->V0(i);
        VertexPointer v1 = f1->V1(i);
        VertexPointer v2 = f1->V2(i);
        VertexPointer v3 = f2->V2(j);

        v0->Q() = m_cv0;
        v1->Q() = m_cv1;
        v2->Q() = m_cv2;
        v3->Q() = m_cv3;

        CoordType n1 = Normal(v0->P(), v3->P(), v2->P());
        CoordType n2 = Normal(v1->P(), v2->P(), v3->P());

        v0->N() = v0->N() - f1->N() - f2->N() + n1;
        v1->N() = v1->N() - f1->N() - f2->N() + n2;
        v2->N() = v2->N() - f1->N() + n1 + n2;
        v3->N() = v3->N() - f2->N() + n1 + n2;

        vcg::face::VFDetach(*f1, (i + 1) % 3);
        vcg::face::VFDetach(*f2, (j + 1) % 3);
        vcg::face::FlipEdge(*this->_pos.F(), this->_pos.E());
        vcg::face::VFAppend(f2, (j + 1) % 3);
        vcg::face::VFAppend(f1, (i + 1) % 3);

        f1->N() = n1;
        f2->N() = n2;

        if (tri::HasPerWedgeTexCoord(m)) {
            f2->WT((j + 1) % 3) = f1->WT((i + 2) % 3);
            f1->WT((i + 1) % 3) = f2->WT((j + 2) % 3);
        }
    }

    bool IsFeasible(BaseParameterClass *ppBase) override
    {
        PlanarEdgeFlipParameter *pp = static_cast<PlanarEdgeFlipParameter *>(ppBase);
        if (!vcg::face::CheckFlipEdge(*this->_pos.F(), this->_pos.E()))
            return false;

        if (math::ToDeg(Angle(this->_pos.FFlip()->cN(), this->_pos.F()->cN()))
            <= pp->CoplanarAngleThresholdDeg) {
            return false;
        }

        const int i = this->_pos.E();
        FacePointer f = this->_pos.F();
        CoordType v0 = f->P0(i);
        CoordType v1 = f->P1(i);
        CoordType v2 = f->P2(i);
        CoordType v3 = f->FFp(i)->P2(f->FFi(i));

        if ((Angle(v2 - v0, v1 - v0) + Angle(v3 - v0, v1 - v0) >= float(M_PI))
            || (Angle(v2 - v1, v0 - v1) + Angle(v3 - v1, v0 - v1) >= float(M_PI))) {
            return false;
        }

        if (!this->_pos.F()->IsW() || !this->_pos.F()->FFp(i)->IsW())
            return false;

        return true;
    }

    ScalarType ComputePriority(BaseParameterClass *pp) override
    {
        if (!this->IsFeasible(pp))
            return std::numeric_limits<ScalarType>::infinity();

        const int i = this->_pos.E();
        FacePointer f1 = this->_pos.F();
        VertexPointer v0 = f1->V0(i);
        VertexPointer v1 = f1->V1(i);
        VertexPointer v2 = f1->V2(i);
        FacePointer f2 = f1->FFp(i);
        VertexPointer v3 = f2->V2(f1->FFi(i));

        const float cbefore = v0->Q() + v1->Q() + v2->Q() + v3->Q();

        CoordType nv0orig = v0->N();
        CoordType nv1orig = v1->N();
        CoordType nv2orig = v2->N();
        CoordType nv3orig = v3->N();

        CoordType n1 = Normal(v0->P(), v3->P(), v2->P());
        CoordType n2 = Normal(v1->P(), v2->P(), v3->P());

        v0->N() = nv0orig - f1->N() - f2->N() + n1;
        v1->N() = nv1orig - f1->N() - f2->N() + n2;
        v2->N() = nv2orig - f1->N() + n1 + n2;
        v3->N() = nv3orig - f2->N() + n1 + n2;

        CurvData cd0 = FaceCurv(v0, v3, v2, n1) + Curvature(v0, f1, f2);
        CurvData cd1 = FaceCurv(v1, v2, v3, n2) + Curvature(v1, f1, f2);
        CurvData cd2 = FaceCurv(v2, v0, v3, n1) + FaceCurv(v2, v3, v1, n2) + Curvature(v2, f1, f2);
        CurvData cd3 = FaceCurv(v3, v2, v0, n1) + FaceCurv(v3, v1, v2, n2) + Curvature(v3, f1, f2);

        v0->N() = nv0orig;
        v1->N() = nv1orig;
        v2->N() = nv2orig;
        v3->N() = nv3orig;

        CURVEVAL curveval;
        m_cv0 = curveval(cd0);
        m_cv1 = curveval(cd1);
        m_cv2 = curveval(cd2);
        m_cv3 = curveval(cd3);
        const float cafter = m_cv0 + m_cv1 + m_cv2 + m_cv3;

        this->_priority = cafter - cbefore;
        return this->_priority;
    }

    static void Init(TRIMESH_TYPE &m, HeapType &heap, BaseParameterClass *pp)
    {
        CURVEVAL curveval;
        heap.clear();

        vcg::tri::UpdateNormal<TRIMESH_TYPE>::PerVertexPerFace(m);

        for (VertexIterator vi = m.vert.begin(); vi != m.vert.end(); ++vi) {
            if (!(*vi).IsD() && (*vi).IsW())
                (*vi).Q() = curveval(Curvature(&(*vi)));
        }

        for (FaceIterator fi = m.face.begin(); fi != m.face.end(); ++fi) {
            if ((*fi).IsD())
                continue;
            for (unsigned int i = 0; i < 3; ++i) {
                if ((*fi).V1(i) - (*fi).V0(i) > 0) {
                    PosType newpos(&*fi, i);
                    TopoEdgeFlip<TRIMESH_TYPE, MYTYPE>::Insert(heap, newpos, tri::IMark(m), pp);
                }
            }
        }
    }
};

} // namespace tri
} // namespace vcg
