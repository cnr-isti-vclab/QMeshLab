/*******************************************************************************
    Copyright (c) 2021, Andrea Maggiordomo, Paolo Cignoni and Marco Tarini

    This file is part of TextureDefrag, a reference implementation for
    the paper ``Texture Defragmentation for Photo-Reconstructed 3D Models''.

    QMeshLab adaptation note: the original TextureObject lazily uploaded QImages
    to OpenGL. In QMeshLab the object is an image/size container; GPU upload is
    deliberately left to the renderer backend.
*******************************************************************************/

#include "texture_object.h"
#include "utils.h"

#include <QImageReader>

#include <algorithm>

TextureObject::TextureObject() = default;
TextureObject::~TextureObject() = default;

bool TextureObject::AddImage(std::string path)
{
    QImageReader reader(QString::fromStdString(path));
    if (!reader.canRead())
        return false;
    TextureImageInfo tii = { QImage(QString::fromStdString(path)) };
    if (tii.texture.isNull())
        return false;
    texInfoVec.push_back(tii);
    texNameVec.push_back(0);
    return true;
}

bool TextureObject::AddImage(const QImage& image)
{
    if (image.isNull())
        return false;
    TextureImageInfo tii = { image };
    texInfoVec.push_back(tii);
    texNameVec.push_back(0);
    return true;
}

void TextureObject::Bind(int) {}
void TextureObject::Release(int) {}

int TextureObject::TextureWidth(std::size_t i)
{
    ensure(i < texInfoVec.size());
    return texInfoVec[i].texture.width();
}

int TextureObject::TextureHeight(std::size_t i)
{
    ensure(i < texInfoVec.size());
    return texInfoVec[i].texture.height();
}

int TextureObject::MaxSize()
{
    int maxsz = 0;
    for (unsigned i = 0; i < ArraySize(); ++i) {
        maxsz = std::max(maxsz, TextureWidth(i));
        maxsz = std::max(maxsz, TextureHeight(i));
    }
    return maxsz;
}

std::vector<TextureSize> TextureObject::GetTextureSizes()
{
    std::vector<TextureSize> texszVec;
    for (unsigned i = 0; i < ArraySize(); ++i)
        texszVec.push_back({TextureWidth(i), TextureHeight(i)});
    return texszVec;
}

std::size_t TextureObject::ArraySize()
{
    return texInfoVec.size();
}

int64_t TextureObject::TextureArea(std::size_t i)
{
    ensure(i < ArraySize());
    return int64_t(TextureWidth(i)) * TextureHeight(i);
}

double TextureObject::GetResolutionInMegaPixels()
{
    int64_t totArea = 0;
    for (unsigned i = 0; i < ArraySize(); ++i)
        totArea += TextureArea(i);
    return totArea / 1000000.0;
}

std::vector<std::pair<double, double>> TextureObject::ComputeRelativeSizes()
{
    std::vector<TextureSize> texSizeVec = GetTextureSizes();
    int maxsz = 0;
    for (auto tsz : texSizeVec) {
        maxsz = std::max(maxsz, tsz.h);
        maxsz = std::max(maxsz, tsz.w);
    }
    if (maxsz <= 0)
        maxsz = 1;

    std::vector<std::pair<double, double>> trs;
    for (auto tsz : texSizeVec) {
        double rw = tsz.w / double(maxsz);
        double rh = tsz.h / double(maxsz);
        trs.push_back(std::make_pair(rw, rh));
    }
    return trs;
}

void Mirror(QImage& img)
{
    int i = 0;
    while (i < (img.height() / 2)) {
        QRgb *line0 = reinterpret_cast<QRgb *>(img.scanLine(i));
        QRgb *line1 = reinterpret_cast<QRgb *>(img.scanLine(img.height() - 1 - i));
        i++;
        for (int j = 0; j < img.width(); ++j)
            std::swap(line0[j], line1[j]);
    }
}
