#pragma once

#include <QImage>
#include <vector>

// PullPush hole-filling algorithm for texture atlases.
// Ported from the original MeshLab pushpull.h (pure Qt, no OpenGL).
namespace vcg {

namespace {

using PushPullByte = unsigned char;

inline int mean4w(int p1, PushPullByte w1, int p2, PushPullByte w2,
                  int p3, PushPullByte w3, int p4, PushPullByte w4)
{
    return (p1 * int(w1) + p2 * int(w2) + p3 * int(w3) + p4 * int(w4))
           / (int(w1) + int(w2) + int(w3) + int(w4));
}

inline QRgb mean4Pixelw(QRgb p1, PushPullByte w1, QRgb p2, PushPullByte w2,
                        QRgb p3, PushPullByte w3, QRgb p4, PushPullByte w4)
{
    int r = mean4w(qRed(p1),   w1, qRed(p2),   w2, qRed(p3),   w3, qRed(p4),   w4);
    int g = mean4w(qGreen(p1), w1, qGreen(p2), w2, qGreen(p3), w3, qGreen(p4), w4);
    int b = mean4w(qBlue(p1),  w1, qBlue(p2),  w2, qBlue(p3),  w3, qBlue(p4),  w4);
    int a = mean4w(qAlpha(p1), w1, qAlpha(p2), w2, qAlpha(p3), w3, qAlpha(p4), w4);
    return qRgba(r, g, b, a);
}

// Generates a weighted mip level from p into mip
inline void PullPushMip(QImage &p, QImage &mip, QRgb bkcolor)
{
    for (int y = 0; y < mip.height(); ++y) {
        for (int x = 0; x < mip.width(); ++x) {
            PushPullByte w1 = (p.pixel(x*2,   y*2  ) == bkcolor) ? 0 : 255;
            PushPullByte w2 = (p.pixel(x*2+1, y*2  ) == bkcolor) ? 0 : 255;
            PushPullByte w3 = (p.pixel(x*2,   y*2+1) == bkcolor) ? 0 : 255;
            PushPullByte w4 = (p.pixel(x*2+1, y*2+1) == bkcolor) ? 0 : 255;
            if (w1 + w2 + w3 + w4 > 0)
                mip.setPixel(x, y, mean4Pixelw(
                    p.pixel(x*2,   y*2  ), w1,
                    p.pixel(x*2+1, y*2  ), w2,
                    p.pixel(x*2,   y*2+1), w3,
                    p.pixel(x*2+1, y*2+1), w4));
        }
    }
}

// Fills holes in p using the mip level
inline void PullPushFill(QImage &p, QImage &mip, QRgb bkg)
{
    const int mw = mip.width();
    const int mh = mip.height();
    for (int y = 0; y < mh; ++y) {
        for (int x = 0; x < mw; ++x) {
            if (p.pixel(x*2, y*2) == bkg)
                p.setPixel(x*2, y*2, mean4Pixelw(
                    mip.pixel(x, y),                              PushPullByte(144),
                    x>0 ? mip.pixel(x-1,y) : bkg,                x>0 ? PushPullByte(48) : 0,
                    y>0 ? mip.pixel(x,y-1) : bkg,                y>0 ? PushPullByte(48) : 0,
                    (x>0&&y>0) ? mip.pixel(x-1,y-1) : bkg,       (x>0&&y>0) ? PushPullByte(16) : 0));
            if (p.pixel(x*2+1, y*2) == bkg)
                p.setPixel(x*2+1, y*2, mean4Pixelw(
                    mip.pixel(x, y),                              PushPullByte(144),
                    x<mw-1 ? mip.pixel(x+1,y) : bkg,             x<mw-1 ? PushPullByte(48) : 0,
                    y>0    ? mip.pixel(x,y-1)  : bkg,             y>0    ? PushPullByte(48) : 0,
                    (x<mw-1&&y>0) ? mip.pixel(x+1,y-1) : bkg,    (x<mw-1&&y>0) ? PushPullByte(16) : 0));
            if (p.pixel(x*2, y*2+1) == bkg)
                p.setPixel(x*2, y*2+1, mean4Pixelw(
                    mip.pixel(x, y),                              PushPullByte(144),
                    x>0    ? mip.pixel(x-1,y)   : bkg,            x>0    ? PushPullByte(48) : 0,
                    y<mh-1 ? mip.pixel(x,y+1)   : bkg,            y<mh-1 ? PushPullByte(48) : 0,
                    (x>0&&y<mh-1) ? mip.pixel(x-1,y+1) : bkg,    (x>0&&y<mh-1) ? PushPullByte(16) : 0));
            if (p.pixel(x*2+1, y*2+1) == bkg)
                p.setPixel(x*2+1, y*2+1, mean4Pixelw(
                    mip.pixel(x, y),                              PushPullByte(144),
                    x<mw-1 ? mip.pixel(x+1,y)   : bkg,            x<mw-1 ? PushPullByte(48) : 0,
                    y<mh-1 ? mip.pixel(x,y+1)   : bkg,            y<mh-1 ? PushPullByte(48) : 0,
                    (x<mw-1&&y<mh-1) ? mip.pixel(x+1,y+1) : bkg, (x<mw-1&&y<mh-1) ? PushPullByte(16) : 0));
        }
    }
}

} // anonymous namespace

// Main PullPush fill entrypoint: fill holes in 'p' (marked by bkcolor) using
// mipmap-based interpolation.
inline void PullPush(QImage &p, QRgb bkcolor)
{
    std::vector<QImage> mip(16);
    int div     = 2;
    int miplev  = 0;

    // Pull phase: build mipmap
    while (true) {
        mip[size_t(miplev)] = QImage(p.width() / div, p.height() / div, p.format());
        mip[size_t(miplev)].fill(bkcolor);
        div *= 2;
        if (miplev > 0)
            PullPushMip(mip[size_t(miplev - 1)], mip[size_t(miplev)], bkcolor);
        else
            PullPushMip(p, mip[0], bkcolor);
        if (mip[size_t(miplev)].width() <= 4 || mip[size_t(miplev)].height() <= 4)
            break;
        ++miplev;
    }
    ++miplev;

    // Push phase: refill
    for (int i = miplev - 1; i >= 0; --i) {
        if (i > 0)
            PullPushFill(mip[size_t(i - 1)], mip[size_t(i)], bkcolor);
        else
            PullPushFill(p, mip[0], bkcolor);
    }
}

} // namespace vcg
