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

#include "texture_packer.hpp"

#include <QPainter>

#include "document.h"

MeshFilterRunResult fail(const QString &message)
{
    MeshFilterRunResult result;
    result.success = false;
    result.documentModified = false;
    result.errorMessage = message;
    return result;
}

MeshFilterRunResult success(const QStringList &info, int newMeshIndex)
{
    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = true;
    result.infoMessages = info;
    if (newMeshIndex >= 0)
        result.newMeshIndices.push_back(newMeshIndex);
    return result;
}

std::vector<QImage> TexturePacker::simplePacking (
    const std::vector<QImage> &srcTexts,
    const int containerNum,
    VCGMesh &mesh)
{

    // The packer assumes that the number of input textures
    // is major or equal than the requested number of outputs.
    TexturePacker packer(srcTexts, containerNum, mesh);
    packer.findBestPlacement();
    packer.updateMeshUV();
    auto output = packer.rasterizeContainers();
    return output;
}

TexturePacker::TexturePacker(
    const std::vector<QImage> &srcTexts,
    int containerNum,
    VCGMesh &mesh)
        : srcNum(srcTexts.size()), containerNum(containerNum), mesh(mesh)
{
    // Note, we are assuming that the number of source textures
    // is major or equal than the number of requested outputs.
    // The check is always done before construction by the static
    // function `pack`.
    //
    // We first initialize both`srcToContainer` and `containerSize`.
    // `srcToContainer` has `srcNum` fixed number of entries; `containerSize`
    // has `containerNum` fixed number of entries.
    //
    // We then distribute the source textures among the container.
    // The distribution is done equally by using integer ceiling division.
    // The last container usually holds a smaller number of sources if the
    // division is not exact.

    srcToContainer.reserve(srcNum);
    containerToSrc.reserve(containerNum);
    for (int i = 0; i < srcNum; ++i) {
        srcToContainer.push_back({ .srcRef = srcTexts[i] } );
    }
    for (int i = 0; i < containerNum; ++i) {
        containerToSrc.push_back({}    );
        containerToSrc[i].srcContained.reserve((srcNum + containerNum - 1) / containerNum);
    }

    // We determine the amount of sources per container dynamically.
    // It this way we guarantee a balanced distribution.
    int srcLeft = srcNum;
    int containerLeft = containerNum;
    int currContainer = 0;
    int srcID = 0;
    while (srcLeft > 0) {
        const int numOfSrcPerContainer = (srcLeft + containerLeft - 1) / containerLeft;

        // For the container we update its mapping by adding
        // the current source to the ones it will pack.
        //
        // For the source texture we construct its map entry
        // by setting its container to `currContainer`.
        for (int i = 0; i < numOfSrcPerContainer; ++i) {
            containerToSrc[currContainer].srcContained.push_back(srcID);
            srcToContainer[srcID].containerID = currContainer;
            srcID++;
        }

        // Update the left counters
        srcLeft -= numOfSrcPerContainer;
        containerLeft -= 1;
        currContainer++;
    }
}

void TexturePacker::findBestPlacement() {
    // For each container we compute the heuristic optimal square which is
    // able to store all the textures associated with that container.
    //
    // This square will have as total area the sum of all sources' area.
    // A side of the square has length equal to the square root of its area.
    //
    // Computed the square, we try to pack the textures within using a bin packer
    // technique via the VCGLib function RectPacker::PackInt. The algorithm returns
    // true if it was able to pack everything. If not, we increment the square's size
    // by 10% and repeat the process until we are able to.
    //
    // The resulting square's size is assigned to the container's entry in `containerToSrc`.
    // The source entries in `srcToContainer` are updated with their new offset positions.
    for (int containerID = 0; containerID < containerToSrc.size(); ++containerID) {

        // If the current container has to pack just a single texture, simply copy it
        // without doing any actual packing.
        if (containerToSrc[containerID].srcContained.size() == 1) {
            const int srcID = containerToSrc[containerID].srcContained[0];
            srcToContainer[srcID].containerOff = vcg::Point2i(0,0);
            containerToSrc[containerID].finalSize = vcg::Point2i(
                srcToContainer[srcID].srcRef.get().size().width(),
                srcToContainer[srcID].srcRef.get().size().height()
                );
            continue;
        }

        // Computing the heuristic square.
        // The algorithm RectPacker requires an array containing all size
        // of the source textures. We construct it in parallel with the computation
        // of the square's area.
        std::vector<vcg::Point2i> srcSizes;
        int squareArea = 0;
        for (const int srcID : containerToSrc[containerID].srcContained) {
            const int srcWidth = srcToContainer[srcID].srcRef.get().size().width();
            const int srcHeight = srcToContainer[srcID].srcRef.get().size().height();
            srcSizes.push_back(vcg::Point2i(srcWidth, srcHeight));

            squareArea += srcWidth * srcHeight;
        }
        const int squareSide = std::sqrt(squareArea);
        vcg::Point2i squareSize(squareSide, squareSide);

        // Trying to pack every texture in the current square
        std::vector<vcg::Point2i> srcOffsets;
        vcg::Point2i boundingBox;
        bool successPacking = false;
        while (!successPacking) {
            // boundingBox contains the bounding box delimiting the
            // square area fitting all the sources within the container.
            //
            // Maybe it could be used to set it as the optimal size
            // of the container
            successPacking = vcg::RectPacker<float>::PackInt(
                srcSizes,
                squareSize,
                srcOffsets,
                boundingBox);

            // If the algorithm wasn't able to pack the textures into the
            // current square, increase its size by 10%.
            if (!successPacking) {
                squareSize.X() *= 1.1;
                squareSize.Y() *= 1.1;
            }
        }
        // Update `containerToSrc` and `srcToContainer` accordingly.
        // `containerToSrc` needs to updated the entry of the current
        // container with its newly computed size.
        //
        // `srcToContainer` needs to update the offsets of the sources
        // that have been placed by the current container. Note that
        // srcOffset isn't indexed in the same way as `srcToContainer`.
        // The i-th entry of srcOffset refers to the source having
        // the ID stored in containerToSrc[containerID].srcContained[i].
        // Graphically:
        //
        // sourceOffset
        // idx:                         |   0      |    1     |    2     | ... |
        // sourceOffset:                | offset_0 | offset_1 | offset_2 | ... |
        //                                   |          |           |      ...
        //                                  \/         \/          \/
        // containerToSrc[containerID]: | 1        |   3       |   5     | ... |
        //
        // So offset_0 is associated to the source of index 1, offset_1 to the source
        // of index 3, and so on...
        //
        containerToSrc[containerID].finalSize = boundingBox;
        for (int i = 0; i < srcOffsets.size(); ++i) {
            const int srcID = containerToSrc[containerID].srcContained[i];
            srcToContainer[srcID].containerOff = srcOffsets[i];
        }
    }
}

void TexturePacker::updateMeshUV() {
    // For each non-deleted face we retrieve its source texture id
    // which is referred to by its UV coordinates. Recall that this value
    // can be retrieved by the `N()` field.
    //
    // We retrieve using `srcToContainer` the container ID and the starting
    // UV position for the coordinates.
    //
    // Note that by default each face stores its UV positions normalized in
    // respect to its source texture image pixel space. So we first need to
    // port them in the source texture image pixel space and then normalize
    // it according to the container's pixel space.
    //
    // Additionally, for the V coordinate there is a subtle quirk: the container
    // is stored as a QImage, this type stores pixels top-down (V = 0 is at the top).
    // The V coordinates in MeshLab are stored bottom-up (V = 0 is at the bottom).
    // Graphically:
    //
    //                  U
    //      _____________>        v ^
    //      |                       |
    //      |                       |
    //      |                       |
    //      |                       |____________>
    //   V \/                                    U
    //          QImage                  MeshLab
    // Thus we need to take into account the conversion while updating the V coordinate.
    for (auto &face : mesh.face) {

        if (face.IsD()) {
            continue;
        }

        const int srcID = face.WT(0).N();
        const int containerID = srcToContainer[srcID].containerID;
        const vcg::Point2i containerOff = srcToContainer[srcID].containerOff;
        const vcg::Point2i containerSize = containerToSrc[containerID].finalSize;
        const vcg::Point2i srcSize = vcg::Point2i(
            srcToContainer[srcID].srcRef.get().width(),
            srcToContainer[srcID].srcRef.get().height()
            );

        for (int k = 0; k < face.VN(); ++k) {
            face.WT(k).N() = containerID;

            // Wrap the original UVs to ensure they are strictly between 0.0 and 1.0
            // This is done because since the original coordinates allowed for wrap back,
            // any value that exceeded the top (i.e., 1.5 would have been treated as 0.5).
            // However, this is not good for our conversion procedure, because 1.5 will be
            // converted in a totally different position within the container's pixel space
            // compared to 0.5.
            //
            // For this reason we enforce that all values are within 0.0 and 1.0.
            const float wrappedU = face.WT(k).U() > 1.0f ? face.WT(k).U() - std::floor(face.WT(k).U()) : face.WT(k).U();
            const float newU = ( ( wrappedU * srcSize.X() ) + containerOff.X() ) / containerSize.X();
            face.WT(k).U() =  newU;

            // The offset for V() needs to be converted to bottom-up (meshlab coordinate system).
            const float wrappedV = face.WT(k).V() > 1.0f ? face.WT(k).V() - std::floor(face.WT(k).V()) : face.WT(k).V();
            const float vOffsetFromBottom = containerSize.Y() - containerOff.Y() - srcSize.Y();
            const float newV = ( ( wrappedV * srcSize.Y() ) + vOffsetFromBottom ) / containerSize.Y();
            face.WT(k).V() = newV;
        }
    }
}

std::vector<QImage> TexturePacker::rasterizeContainers() {
    // and to add a description to the static method `simplePacking`

    // The function assumes all source textures share the same format.
    //
    // For each container we generate one blank image having the corresponding
    // size. Recall that the size of each container is stored in the corresponding
    // entry in `containerToSrc`.
    //
    // Then we retrieve the set of sources that must be painted onto the image.
    // Recall that this set (stored by the indices of the sources) is in the
    // corresponding field of the container's entry in `containerToSrc`.
    //
    // For each source texture to paint, we draw it starting at its upper-left
    // position within the container. Recall that this value is stored in the
    // corresponding field on the source's entry in `srcToContainer`.
    const QImage::Format format = srcToContainer[0].srcRef.get().format();
    std::vector<QImage> rasterContainers;

    for (const auto &container : containerToSrc) {

        // Construct the blank image
        const QSize containerSize = {
            container.finalSize.X(),
            container.finalSize.Y()
        };
        QImage currImage(containerSize, format);
        currImage.fill(0);

        // Fill the blank image with its source textures
        QPainter containerPainter(&currImage);

        for (const auto &srcID : container.srcContained) {
            const QImage &srcImage = srcToContainer[srcID].srcRef.get();
            // By setting `CompositionMode_Source` we guarantee that the
            // pixels from the source image will overwrite the blank pixels
            // of the container directly, without any alpha blending.
			containerPainter.setCompositionMode (QPainter::CompositionMode_Source);
            const QPoint leftCornerPos =
                {
                srcToContainer[srcID].containerOff.X(),
                srcToContainer[srcID].containerOff.Y()
                };
            containerPainter.drawImage(leftCornerPos, srcImage);
        }
        containerPainter.end();

        // Store the rasterized container in the result
        rasterContainers.push_back(currImage);
    }

    return rasterContainers;
}
