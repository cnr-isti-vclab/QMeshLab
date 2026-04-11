#include "linerenderer.h"

#include <array>

namespace LineRenderer {

namespace {
constexpr std::array<std::array<float, 2>, 6> kFatTriTemplate = {{
    {{ 0.0f, -1.0f }},
    {{ 0.0f, 1.0f }},
    {{ 1.0f, -1.0f }},
    {{ 1.0f, -1.0f }},
    {{ 0.0f, 1.0f }},
    {{ 1.0f, 1.0f }},
}};
} // namespace

void appendFatLineSegmentVertices(std::vector<float> &dst,
                                  float p0x,
                                  float p0y,
                                  float p0z,
                                  float p1x,
                                  float p1y,
                                  float p1z)
{
    for (const auto &tpl : kFatTriTemplate) {
        dst.push_back(p0x);
        dst.push_back(p0y);
        dst.push_back(p0z);
        dst.push_back(p1x);
        dst.push_back(p1y);
        dst.push_back(p1z);
        dst.push_back(tpl[0]); // along (0=start, 1=end)
        dst.push_back(tpl[1]); // side (-1/+1)
    }
}

std::vector<float> buildFatLineVertices(const std::vector<float> &lineSegments)
{
    const size_t segmentCount = lineSegments.size() / kLineStrideFloats;
    if (segmentCount == 0)
        return {};

    std::vector<float> fatData;
    fatData.reserve(segmentCount * 6 * kFatLineStrideFloats);

    for (size_t si = 0; si < segmentCount; ++si) {
        const float p0x = lineSegments[si * kLineStrideFloats + 0];
        const float p0y = lineSegments[si * kLineStrideFloats + 1];
        const float p0z = lineSegments[si * kLineStrideFloats + 2];
        const float p1x = lineSegments[si * kLineStrideFloats + 3];
        const float p1y = lineSegments[si * kLineStrideFloats + 4];
        const float p1z = lineSegments[si * kLineStrideFloats + 5];
        appendFatLineSegmentVertices(fatData, p0x, p0y, p0z, p1x, p1y, p1z);
    }

    return fatData;
}

} // namespace LineRenderer

