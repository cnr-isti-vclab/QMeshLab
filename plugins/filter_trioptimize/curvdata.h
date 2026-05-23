/****************************************************************************
* VCGLib                                                            o o     *
* Visual and Computer Graphics Library                            o     o   *
*                                                                _   O  _   *
* Copyright(C) 2004                                                \/)\/    *
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

#include <vcg/math/base.h>
#include <cmath>

namespace vcg {

class CurvData
{
public:
    CurvData() = default;

    friend const CurvData operator+(const CurvData &lhs, const CurvData &rhs)
    {
        CurvData result;
        result.A = lhs.A + rhs.A;
        result.H = lhs.H + rhs.H;
        result.K = lhs.K + rhs.K;
        return result;
    }

    friend CurvData &operator+=(CurvData &lhs, const CurvData &rhs)
    {
        lhs.A += rhs.A;
        lhs.H += rhs.H;
        lhs.K += rhs.K;
        return lhs;
    }

    float A = 0.0f; // area
    float H = 0.0f; // absolute mean curvature
    float K = 0.0f; // gaussian curvature
};

class NSMCEval
{
public:
    float operator()(const CurvData &c)
    {
        return std::pow(c.H / 4.0f, 2.0f) / c.A;
    }
};

class MeanCEval
{
public:
    float operator()(const CurvData &c)
    {
        return c.H / 4.0f;
    }
};

class AbsCEval
{
public:
    float operator()(const CurvData &c)
    {
        const float k = 2.0f * float(M_PI) - c.K;
        if (k > 0.0f)
            return 2.0f * (c.H / 4.0f);
        return 2.0f * math::Sqrt(std::pow(c.H / 4.0f, 2.0f) - (c.A * k));
    }
};

} // namespace vcg
