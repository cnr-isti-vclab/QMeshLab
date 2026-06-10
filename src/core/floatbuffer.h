#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <vector>

// Float image buffer used for per-camera depth and silhouette computations.
// Ported from the original MeshLab floatbuffer.h/.cpp, removing file I/O and
// replacing vcg::Histogram with std::nth_element.
class FloatBuffer
{
public:
    float *data = nullptr;
    int    sx   = 0;
    int    sy   = 0;

    FloatBuffer() = default;

    explicit FloatBuffer(const FloatBuffer &from) : sx(from.sx), sy(from.sy)
    {
        data = new float[sx * sy];
        for (int i = 0; i < sx * sy; ++i)
            data[i] = from.data[i];
    }

    ~FloatBuffer() { delete[] data; }

    FloatBuffer &operator=(const FloatBuffer &) = delete;

    bool init(int sizex, int sizey)
    {
        if (data != nullptr)
            return false;
        sx   = sizex;
        sy   = sizey;
        data = new float[sx * sy];
        return true;
    }

    // Returns 0 for out-of-bounds accesses
    float getval(int xx, int yy) const
    {
        if (xx < 0 || yy < 0 || xx >= sx || yy >= sy)
            return 0.0f;
        return data[(yy * sx) + xx];
    }

    void setval(int xx, int yy, float val)
    {
        if (xx < 0 || yy < 0 || xx >= sx || yy >= sy)
            return;
        data[(yy * sx) + xx] = val;
    }

    void fillwith(float val)
    {
        for (int i = 0; i < sx * sy; ++i)
            data[i] = val;
    }

    // Compute Sobel-filtered gradient magnitude into this buffer from 'from'
    void applysobel(const FloatBuffer &from)
    {
        fillwith(0.0f);
        for (int xx = 1; xx < sx - 1; ++xx) {
            for (int yy = 1; yy < sy - 1; ++yy) {
                if (from.getval(xx, yy) != 0.0f) {
                    float accum = 0.0f;
                    accum += -1.0f * from.getval(xx - 1, yy - 1);
                    accum += -2.0f * from.getval(xx - 1, yy    );
                    accum += -1.0f * from.getval(xx - 1, yy + 1);
                    accum += +1.0f * from.getval(xx + 1, yy - 1);
                    accum += +2.0f * from.getval(xx + 1, yy    );
                    accum += +1.0f * from.getval(xx + 1, yy + 1);
                    data[(yy * sx) + xx] += std::abs(accum);
                }
            }
        }
        for (int xx = 1; xx < sx - 1; ++xx) {
            for (int yy = 1; yy < sy - 1; ++yy) {
                if (from.getval(xx, yy) != 0.0f) {
                    float accum = 0.0f;
                    accum += -1.0f * from.getval(xx - 1, yy - 1);
                    accum += -2.0f * from.getval(xx    , yy - 1);
                    accum += -1.0f * from.getval(xx + 1, yy - 1);
                    accum += +1.0f * from.getval(xx - 1, yy + 1);
                    accum += +2.0f * from.getval(xx    , yy + 1);
                    accum += +1.0f * from.getval(xx + 1, yy + 1);
                    data[(yy * sx) + xx] += std::abs(accum);
                }
            }
        }
    }

    // Classify pixels as outside (-1), border (0), or interior (large positive).
    // Uses the 90th percentile of the Sobel buffer to distinguish border from interior.
    // 'zerofrom' is the depth buffer; pixels where zerofrom==0 are outside the object.
    void initborder(const FloatBuffer &zerofrom)
    {
        // Find 90th percentile of non-zero Sobel values via nth_element
        std::vector<float> nonzero;
        nonzero.reserve(size_t(sx * sy));
        for (int kk = 0; kk < sx * sy; ++kk)
            if (data[kk] != 0.0f)
                nonzero.push_back(data[kk]);

        float bthreshold = 0.0f;
        if (!nonzero.empty()) {
            const auto idx = std::min(
                size_t(0.90f * float(nonzero.size())),
                nonzero.size() - 1u);
            std::nth_element(
                nonzero.begin(),
                nonzero.begin() + ptrdiff_t(idx),
                nonzero.end());
            bthreshold = nonzero[idx];
        }

        for (int kk = 0; kk < sx * sy; ++kk) {
            if (zerofrom.data[kk] == 0.0f)    // outside object
                data[kk] = -1.0f;
            else if (data[kk] > bthreshold)   // depth-discontinuity border
                data[kk] = 0.0f;
            else                              // interior pixel
                data[kk] = 10000000.0f;
        }
    }

    // BFS distance transform from border (0) pixels.
    // Returns the maximum distance found, or -10000 if buffer is empty.
    float distancefield()
    {
        std::queue<int> todo;
        float maxval = -10000.0f;

        for (int kk = 0; kk < sx * sy; ++kk)
            if (data[kk] == 0.0f)
                todo.push(kk);

        while (!todo.empty()) {
            const int   idx     = todo.front();
            todo.pop();
            const int   yy      = idx / sx;
            const int   xx      = idx % sx;
            const float nextval = data[idx] + 1.0f;

            auto tryUpdate = [&](int cx, int cy) {
                if (cx < 0 || cx >= sx || cy < 0 || cy >= sy)
                    return;
                const int nidx = cx + (sx * cy);
                if (data[nidx] != -1.0f && data[nidx] > nextval) {
                    data[nidx] = nextval;
                    todo.push(nidx);
                    if (nextval > maxval)
                        maxval = nextval;
                }
            };

            tryUpdate(xx - 1, yy    );
            tryUpdate(xx + 1, yy    );
            tryUpdate(xx    , yy - 1);
            tryUpdate(xx    , yy + 1);
        }

        return maxval;
    }
};
