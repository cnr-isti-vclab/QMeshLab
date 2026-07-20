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

#ifndef MESHLAB_TEXTURE_PACKER_HPP
#define MESHLAB_TEXTURE_PACKER_HPP
#include <functional>
#include <QImage>

// #include "common/mlexception.h"
// #include "common/ml_document/mesh_model.h"
#include "meshfilterplugin.h"
#include "vcgmesh.h"
#include "vcg/space/rect_packer.h"

/**
 * Utility class that let the user position n source textures into
 * m destination containers (i.e., output textures) where m is
 * always minor or equal than n.
 *
 * The implementation tries to distribute the inputs evenly across
 * the containers.
 *
 * Note that the class updates also the mesh referring to the original
 * source textures such that its UV coordinates reflect the new layout.
 */
class TexturePacker {
    public:

        // ================ PUBLIC METHODS ================

        /*!
         * Given a set of source texture images, it tries to compact them
         * into `containerNum` output texture images.
         *
         * The algorithm is executed only if the number of `srcTextures` is
         * strictly major than the number of containers, otherwise it exits
         * immediately.
         *
         * It also updates the UV coordinates of mesh referring to said texture
         * images.
         *
         * @param srcTexts: the array of source texture images to compact.
         * @param containerNum: the number of final output textures.
         * @param mesh: the mesh having its UV coordinates referring to said
         *              texture images.
         *
         * @return the array of compacted output textures of size `containerNum`.
         */
        static std::vector<QImage> simplePacking (
                const std::vector<QImage> &srcTexts,
                const int containerNum,
                VCGMesh &mesh);

    private:

        // ================ PRIVATE STRUCTS AND CLASSES ================
        /**
         * Holder storing for a source texture all the information regarding
         * how it is stored within its container.
         *
         * Its fields are:
         *
         * - srcRef: holds a reference to the original raw input texture image.
         *
         * - containerID: the index of the container texture assigned to the source
         *                by TexturePacker.
         *
         * - containerOff: the pixel position of the upper-left corner of the
         *                 source texture in the container holding it.
         */
        struct SrcInfo {
            std::reference_wrapper<const QImage> srcRef;
            int containerID = -1;
            vcg::Point2i containerOff = vcg::Point2i(0,0);
        };

        /* Holder storing for a container texture all the information necessary
         * for constructing it.
         *
         * Its fields are:
         *
         * - finalSize: the final width and height of the container.
         *              At the start it is initialized with the
         *              default value (0,0).
         *
         * - srcPlaced: the set of sources (stored by their IDs) that have
         *              been placed within the container.
         */
        struct ContainerInfo {
            vcg::Point2i finalSize = vcg::Point2i(0,0);
            std::vector<int> srcContained;
        };

        // ================ PRIVATE FIELDS ================
        // - srcNum: the number of source textures. It must always be major or equal
        //           than the number of containers.
        //
        // - containerNum: the number of container outputs. it must always be minor
        //                  or equal than the number of source textures.
        //
        // - mesh: a reference to the mesh whose UV parameterization depends on the
        //         sources. At the end of the packing all its UV coordinates must be
        //         updated according to the new containers.
        //
        // - srcToContainer maps each source texture (indexed by its ID) to all its
        //   information regarding the container that holds it.
        //
        // - containerToSrc keeps for each container (indexed by its ID) the final
        //   dimensions and the list of source textures (represented by their ID)
        //   that have been placed within.
        const int srcNum;
        const int containerNum;
        VCGMesh &mesh;
        std::vector<SrcInfo> srcToContainer;
        std::vector<ContainerInfo> containerToSrc;

        // ================ PRIVATE METHODS ================

        /*!
         * It constructs a base instance filling the initial mappings for the sources and the containers.
         * No packing has been done after the construction, it only determines which sources go to which
         * container.
         */
        TexturePacker (
            const std::vector<QImage> &srcTexts,
            const int containerNum,
            VCGMesh &mesh);

        /*!
         * Updates the mesh's texture coordinates according to the new
         * containers.
         */
        void updateMeshUV ();

        /*!
         * Finds the best-fit placement for all the containers
         * to hold their assigned source textures.
         *
         * The generated size for the container will always be a square.
         *
         * The algorithm uses the bin-packing strategy implemented by the
         * VCGLib function `PackInt`.
         */
        void findBestPlacement ();

        /*!
         * Generates for each container the final texture image having pixels
         * copied from its source textures.
         *
         * @return the array of containers' image, ordered in respect to their IDs.
         */
        std::vector<QImage> rasterizeContainers ();
};


#endif //MESHLAB_TEXTURE_PACKER_HPP
