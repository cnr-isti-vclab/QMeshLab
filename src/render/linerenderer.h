#pragma once

#include <vector>

namespace LineRenderer {

constexpr int kLineStrideFloats = 6;    // p0.xyz + p1.xyz
constexpr int kFatLineStrideFloats = 8; // p0.xyz + p1.xyz + along + side

void appendFatLineSegmentVertices(std::vector<float> &dst,
                                  float p0x,
                                  float p0y,
                                  float p0z,
                                  float p1x,
                                  float p1y,
                                  float p1z);

std::vector<float> buildFatLineVertices(const std::vector<float> &lineSegments);

} // namespace LineRenderer

