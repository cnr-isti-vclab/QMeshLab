#ifndef __VCGFOREMBREE_H
#define __VCGFOREMBREE_H

#include <iostream>
#include <vcg/complex/complex.h>
#include <vcg/simplex/face/pos.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <vcg/complex/algorithms/closest.h>

#include <vcg/complex/algorithms/update/color.h>
#include <vcg/complex/algorithms/update/quality.h>
#include <wrap/callback.h>
#include <embree4/rtcore.h>
#include <vcg/math/gen_normal.h>
#include <limits>
#include <cmath>
#include <ctime>
#include <omp.h>
#include <tuple>
#include <algorithm>






/*
    @Author: Paolo Fasano
    @Description: This class aims to integrate intel embree4 with the vcglib giving some basic methods that can be used to build more complex features.
*/
namespace vcg{
    template <class MeshType>
    class EmbreeAdaptor{

        RTCDevice device = rtcNewDevice(nullptr);
        RTCScene scene = nullptr;
        // Derived from the mesh bounding box in loadVCGMeshInScene; see rayEpsilon.
        float autoRayEpsilon = 1e-4f;
        public:
            // Number of chunks the face array is split into for progress reporting.
            // Higher values give more frequent callback updates and finer cancellation
            // granularity, at the cost of slightly more synchronisation overhead.
            int callbackChunkCount = 24;

            // Minimum ray distance (tnear) used when shooting rays from face barycenters.
            // Acts as a self-intersection offset: the ray starts this far from the surface
            // so it does not immediately hit the face it originates from.
            //
            // Zero - the default - derives it from the mesh bounding box, which is what
            // you almost always want. The barycenter is computed in float, so its distance
            // from the true face plane is a few ULPs of the coordinate magnitude and thus
            // grows with the absolute scale of the mesh. A fixed offset silently stops
            // working on meshes far from the origin: every ray then hits its own face and
            // the surface reads as fully occluded (scattered black faces).
            //
            // Set a positive value to override with an absolute distance.
            float rayEpsilon = 0.0f;

            EmbreeAdaptor() : scene(rtcNewScene(device)) {}

            // Self-intersection offset actually used by the algorithms: the explicit
            // rayEpsilon when the caller set one, otherwise the value derived from the
            // mesh bounding box at scene load. See rayEpsilon.
            float effectiveRayEpsilon() const {
                return rayEpsilon > 0.0f ? rayEpsilon : autoRayEpsilon;
            }

            // preprocess=true recomputes normals/bbox/flags on the mesh (needed by
            // the AO/SDF/obscurance algorithms). Pass false for pure ray-cast use
            // (e.g. occlusion queries): the scene needs only positions + indices,
            // so the mesh is left untouched.
            EmbreeAdaptor(MeshType &m, bool preprocess = true) {
                scene = loadVCGMeshInScene(m, preprocess);
            }

            ~EmbreeAdaptor() {
                release_global_resources();
            }

        /*
        @Author: Paolo Fasano
        @Parameter: Point3f rayDirection, direction the rays are shoot towards
        @Description: foreach face the barycenter is found and a single ray is shoot. If the ray intersect with
            something the face color is set to black else is set to white.
        */
        public:
         void selectVisibleFaces(
             MeshType &m,
             Point3f rayDirection,
             bool incrementalSelect,
             CallBackPos *cb = nullptr){

            const int faceCount = m.FN();
            if (faceCount <= 0) {
                release_global_resources();
                return;
            }

            if (cb && !(*cb)(0, "Selecting visible faces")) {
                release_global_resources();
                return;
            }

            if (incrementalSelect == false){
                //deselect all previously selected faces
                for(int i = 0;i<faceCount; i++){
                    if(m.face[i].IsS()){
                        m.face[i].ClearS();
                    }
                }
            }

            Point3f normalizedDir = rayDirection;
            const int blockSize = (cb != nullptr) ? std::max(1, faceCount / callbackChunkCount) : faceCount;
            bool interrupted = false;

            auto computeFace = [&](int i) {
                RTCRayHit rayhit = initRayValues();
                Point3f b = vcg::Barycenter(m.face[i]);
                Point3f dir = normalizedDir;

                // Was a hardcoded tnear of 4: an absolute distance that skips the
                // whole model on a small mesh and self-intersects on a large one.
                rayhit = setRayValues(b, dir, effectiveRayEpsilon());

                RTCRayQueryContext context;
                rtcInitRayQueryContext(&context);

                RTCIntersectArguments intersectArgs;
                rtcInitIntersectArguments(&intersectArgs);
                intersectArgs.context = &context;

                rtcIntersect1(scene, &rayhit, &intersectArgs);

                //if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID)
                if (rayhit.ray.tfar == std::numeric_limits<float>::infinity())
                    m.face[i].SetS();
            };

            for (int begin = 0; begin < faceCount && !interrupted; begin += blockSize) {
                const int end = std::min(faceCount, begin + blockSize);
                #pragma omp parallel for
                for (int i = begin; i < end; ++i)
                    computeFace(i);

                if (cb) {
                    const int pos = int((100.0 * double(end)) / double(faceCount));
                    const std::string msg = vcg::StrFormat("Selecting visible faces (%d/%d)", end, faceCount);
                    if (!(*cb)(pos, msg.c_str()))
                        interrupted = true;
                }
            }

            release_global_resources();
        }

        /*
        @Description: perspective visibility query. Returns true if the open segment
            between 'from' and 'to' is unobstructed by the scene geometry, i.e. 'to'
            is directly visible from 'from'. The endpoints are excluded via rayEpsilon
            so a segment ending on a surface face does not report itself as occluder.
            Unlike the other methods this does NOT release the scene, so the adaptor
            can be kept alive and queried many times (e.g. viewpoint occlusion tests).
        */
        public:
         bool segmentUnoccluded(Point3f from, Point3f to){
            const float epsilon = effectiveRayEpsilon();
            Point3f dir = to - from;
            const float dist = dir.Norm();
            if (dist <= epsilon)
                return true;
            dir /= dist;

            RTCRayHit rayhit = setRayValues(from, dir, epsilon, dist - epsilon);
            RTCRayQueryContext context;
            rtcInitRayQueryContext(&context);
            RTCIntersectArguments intersectArgs;
            rtcInitIntersectArguments(&intersectArgs);
            intersectArgs.context = &context;

            rtcIntersect1(scene, &rayhit, &intersectArgs);
            return rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID;
        }

        /*
        @Author: Paolo Fasano
        @Parameter: MeshType &m, reference to a mesh
        @Description: this method apply some preprocessing over it using standard vcglib methods.
            Than the mesh is loaded as a new embree geometry inside a new embree scene. The new embree variables
            are global to the class in order to be used with the other methods.
        */
        public:
            RTCScene loadVCGMeshInScene(MeshType &m, bool preprocess = true){

            RTCScene loaded_scene = rtcNewScene(device);
            RTCGeometry geometry = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);

            //a little mesh preprocessing before adding it to a RTCScene (skipped
            //for pure ray-cast use, which reads only positions + indices)
            if (preprocess) {
                tri::RequirePerVertexNormal(m);
                tri::UpdateNormal<MeshType>::PerVertexNormalized(m);
                tri::UpdateNormal<MeshType>::PerFaceNormalized(m);
                tri::UpdateBounding<MeshType>::Box(m);
                tri::UpdateFlags<MeshType>::FaceClearV(m);
            } else {
                // Positions are all the ray-cast paths read, but the derived epsilon
                // below still needs a bounding box.
                tri::UpdateBounding<MeshType>::Box(m);
            }

            // A few ULPs of the coordinate magnitude is the actual requirement; the
            // diagonal is the available proxy for it, and 1e-5 of it leaves ~2 orders
            // of magnitude of headroom over float precision while staying far below
            // any real feature size. The floor keeps tnear positive on a degenerate box.
            {
                const float diag = m.bbox.IsNull() ? 0.0f : m.bbox.Diag();
                autoRayEpsilon = std::max(diag * 1e-5f, 1e-6f);
            }

            float* vb = (float*) rtcSetNewGeometryBuffer(geometry, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 3*sizeof(float), m.VN());
            for (int i = 0;i<m.VN(); i++){
                vb[i*3]=m.vert[i].P()[0];
                vb[i*3+1]=m.vert[i].P()[1];
                vb[i*3+2]=m.vert[i].P()[2];
            }

            unsigned* ib = (unsigned*) rtcSetNewGeometryBuffer(geometry, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3*sizeof(unsigned), m.FN());
            for(int i = 0;i<m.FN(); i++){
                ib[i*3] = tri::Index(m,m.face[i].V(0));
                ib[i*3+1] = tri::Index(m,m.face[i].V(1));
                ib[i*3+2] = tri::Index(m,m.face[i].V(2));
            }

            rtcCommitGeometry(geometry);
            rtcAttachGeometry(loaded_scene, geometry);
            rtcReleaseGeometry(geometry);
            rtcCommitScene(loaded_scene);

            return loaded_scene;
        }

        /*
        @Author: Paolo Fasano
        @Parameter: MeshType &m, reference to a mesh.
        @Parameter: int nRay, number of rays that must be generated and shoot.
        @Description: for each face from the barycenter this method shoots n rays towards a generated direction(to infinity).
            If the ray direction is not pointing inside than the ray is actually shoot.
            If the ray intersect something than the face quality of the mesh is updated with the normal of the fica multiplied by the direction.
        */
        public:
         void computeAmbientOcclusion(MeshType &inputM, int nRay, CallBackPos *cb = nullptr){
            std::vector<Point3f> unifDirVec;
            GenNormal<float>::Fibonacci(nRay,unifDirVec);
            computeAmbientOcclusion(inputM, unifDirVec, cb);
        }

        /*
        @Author: Paolo Fasano
        @Parameter: MeshType &m,reference to a mesh.
        @Parameter: std::vector<Point3f> unifDirVec, vector of direction specified by the user.
        @Description: for each face from the barycenter this method shoots n rays towards some user generated directions(to infinity).
            If the ray direction is not pointing inside than the ray is actually shoot.
            If the ray intersect something than the face quality of the mesh is updated with the normal of the fica multiplied by the direction.

            One more operation done in the AmbientOcclusion is to calculate the bent normal foreach face and save it in an attribute named "BentNormal"
        */
        public:
         void computeAmbientOcclusion(
             MeshType &inputM,
             std::vector<Point3f> unifDirVec,
             CallBackPos *cb = nullptr){
            if (cb && !(*cb)(0, "Computing ambient occlusion")) {
                release_global_resources();
                return;
            }
            tri::UpdateQuality<MeshType>::FaceConstant(inputM,0);
            typename MeshType::template PerFaceAttributeHandle<Point3f> bentNormal = vcg::tri::Allocator<MeshType>:: template GetPerFaceAttribute<Point3f>(inputM, std::string("BentNormal"));

            const int faceCount = inputM.FN();
            if (faceCount <= 0) {
                release_global_resources();
                return;
            }
            const int blockSize = (cb != nullptr) ? std::max(1, faceCount / callbackChunkCount) : faceCount;
            bool interrupted = false;
            const float epsilon = effectiveRayEpsilon();

            auto computeFace = [&](int i) {
                RTCRayHit rayhit = initRayValues();
                Point3f b = vcg::Barycenter(inputM.face[i]);
                updateRayOrigin(rayhit, b);
                rayhit.ray.tnear  = epsilon;

                Point3f bN;
                int accRays=0;
                for(int r = 0; r<int(unifDirVec.size()); r++){
                    Point3f dir = unifDirVec[r];
                    float scalarP = inputM.face[i].N()*dir;

                    if(scalarP>0){

                        updateRayDirection(rayhit, dir);
                        rayhit.ray.tfar   = std::numeric_limits<float>::infinity();
                        rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;

                        RTCRayQueryContext context;
                        rtcInitRayQueryContext(&context);

                        RTCIntersectArguments intersectArgs;
                        rtcInitIntersectArguments(&intersectArgs);
                        intersectArgs.context = &context;

                        rtcIntersect1(scene, &rayhit, &intersectArgs);

                        if (rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID){
                            bN+=dir;
                            accRays++;
                            inputM.face[i].Q()+=scalarP;
                        }

                    }
                }
                if (accRays > 0)
                    bentNormal[i] = bN/accRays;
                else
                    bentNormal[i] = Point3f(0,0,0);
            };

            for (int begin = 0; begin < faceCount && !interrupted; begin += blockSize) {
                const int end = std::min(faceCount, begin + blockSize);
                #pragma omp parallel for
                for(int i = begin; i < end; i++)
                    computeFace(i);

                if (cb) {
                    const int pos = int((100.0 * double(end)) / double(faceCount));
                    const std::string msg = vcg::StrFormat("Computing ambient occlusion (%d/%d)", end, faceCount);
                    if (!(*cb)(pos, msg.c_str()))
                        interrupted = true;
                }
            }

            if (!interrupted)
                tri::UpdateColor<MeshType>::PerFaceQualityGray(inputM);
            release_global_resources();
        }

        /*
        @Author: Paolo Fasano
        @Parameter: MeshType &m, reference to a mesh.
        @Parameter: int nRay, number of rays that must be generated and shoot.
        @Parameter: float Tau, the grater this value is the grater the influence of the rays that intersect with some face
        @Description: for each face from the barycenter this method shoots n rays towards a generated direction(to infinity).
            If the ray direction is not pointing inside than the ray is actually shoot.
            If the ray intersect something than the face quality of the mesh is updated with the normal of the fica multiplied by the direction;
            else, if there are no hits, the face get updated of 1-distanceHit^tau
        */
        public:
         void computeObscurance(MeshType &inputM, int nRay, float tau, CallBackPos *cb = nullptr){
            std::vector<Point3f> unifDirVec;
                GenNormal<float>::Fibonacci(nRay,unifDirVec);

           computeObscurance(inputM, unifDirVec, tau, cb);
        }

        /*
        @Author: Paolo Fasano
        @Parameter: MeshType &m, reference to a mesh.
        @Parameter: std::vector<Point3f> unifDirVec, vector of direction specified by the user.
        @Parameter: float Tau, the grater this value is the grater the influence of the rays that intersect with some face
        @Description: for each face from the barycenter this method shoots n rays towards a generated direction(to infinity).
            If the ray direction is not pointing inside than the ray is actually shoot.
            If the ray intersect something than the face quality of the mesh is updated with the normal of the fica multiplied by the direction;
            else, if there are no hits, the face get updated of 1-distanceHit^tau
        */
        public:
         void computeObscurance(
             MeshType &inputM,
             std::vector<Point3f> unifDirVec,
             float tau,
             CallBackPos *cb = nullptr){
            if (cb && !(*cb)(0, "Computing obscurance")) {
                release_global_resources();
                return;
            }
            tri::UpdateQuality<MeshType>::FaceConstant(inputM,0);

            const int faceCount = inputM.FN();
            if (faceCount <= 0) {
                release_global_resources();
                return;
            }
            const int blockSize = (cb != nullptr) ? std::max(1, faceCount / callbackChunkCount) : faceCount;
            bool interrupted = false;
            const float epsilon = effectiveRayEpsilon();

            auto computeFace = [&](int i) {
                RTCRayHit rayhit = initRayValues();
                Point3f b = vcg::Barycenter(inputM.face[i]);
                updateRayOrigin(rayhit, b);
                rayhit.ray.tnear  = epsilon;

                for(int r = 0; r<int(unifDirVec.size()); r++){
                    Point3f dir = unifDirVec[r];
                    float scalarP = inputM.face[i].N()*dir;

                    if(scalarP>0){
                        updateRayDirection(rayhit, dir);
                        rayhit.ray.tfar   = std::numeric_limits<float>::infinity();
                        rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;

                        RTCRayQueryContext context;
                        rtcInitRayQueryContext(&context);

                        RTCIntersectArguments intersectArgs;
                        rtcInitIntersectArguments(&intersectArgs);
                        intersectArgs.context = &context;

                        rtcIntersect1(scene, &rayhit, &intersectArgs);

                        if (rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
                            inputM.face[i].Q()+=scalarP;
                        else
                            inputM.face[i].Q()+=(1-std::pow(rayhit.ray.tfar,tau));

                    }
                }
            };

            for (int begin = 0; begin < faceCount && !interrupted; begin += blockSize) {
                const int end = std::min(faceCount, begin + blockSize);
                #pragma omp parallel for
                for(int i = begin; i < end; i++)
                    computeFace(i);

                if (cb) {
                    const int pos = int((100.0 * double(end)) / double(faceCount));
                    const std::string msg = vcg::StrFormat("Computing obscurance (%d/%d)", end, faceCount);
                    if (!(*cb)(pos, msg.c_str()))
                        interrupted = true;
                }
            }
            if (!interrupted)
                tri::UpdateColor<MeshType>::PerFaceQualityGray(inputM);
            release_global_resources();
        }

        /*
        @Author: Paolo Fasano
        @Parameter: MeshType &m, reference to a mesh.
        @Parameter: int nRay, number of rays that must be generated and shoot.
        @Parameter: float degree, this variable represents the angle of the cone for which we consider a point as a valid direction
        @Description:
            - Use a cone centered around the inward-normal direction of a point on a surface mesh.
            - Send rays inside the cone to the other side of the mesh.
            - Calculate the SDF at a point as the weighted average of all ray lengths within one
                standard deviation from the median of all lengths.
            - Use inverse angle between the ray and the center of the cone as the weight.
            - The SDF is invariant to rigid body transformations of the whole mesh and oblivious to local deformations.
            - Small cone angles are too sensitive to local features, while large opening angles close to 180◦
                expose the SDF measure to noise and errors.

        */
        public:
         void computeSDF(MeshType &inputM, int nRay, float degree, CallBackPos *cb = nullptr){

            if (cb && !(*cb)(0, "Computing shape diameter")) {
                release_global_resources();
                return;
            }

            if (degree >= 180)
                degree = 120;

            tri::UpdateQuality<MeshType>::FaceConstant(inputM,0);

            //first step is to generate the cone of rays, i will generate a sphear and for each face take only the
            //the ones that fall in the desired degree (in respect to the opposite of face norm)
            std::vector<Point3f> unifDirVec;
            GenNormal<float>::Fibonacci(nRay,unifDirVec);
            const int faceCount = inputM.FN();
            const int blockSize = (cb != nullptr) ? std::max(1, faceCount / callbackChunkCount) : faceCount;
            bool interrupted = false;
            const float epsilon = effectiveRayEpsilon();

            for (int i = 0; i < faceCount; i++)
            {
                RTCRayHit rayhit = initRayValues();
                Point3f b = vcg::Barycenter(inputM.face[i]);
                updateRayOrigin(rayhit, b);
                rayhit.ray.tnear  = epsilon;

                float weight_sum = 0;
                float weighted_sum = 0;

                for(int r = 0; r < int(unifDirVec.size()); r++){
                    Point3f dir = unifDirVec[r];
                    float scalarP = inputM.face[i].N()*dir;

                    float angle_dir_b = Angle(b, dir);

                    if (scalarP < 0 && vcg::math::ToRad(angle_dir_b) <= degree){
                        updateRayDirection(rayhit, dir);
                        rayhit.ray.tfar   = std::numeric_limits<float>::infinity();
                        rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;

                        RTCRayQueryContext context;
                        rtcInitRayQueryContext(&context);

                        RTCIntersectArguments intersectArgs;
                        rtcInitIntersectArguments(&intersectArgs);
                        intersectArgs.context = &context;

                        rtcIntersect1(scene, &rayhit, &intersectArgs);

                        if (rayhit.ray.tfar != std::numeric_limits<float>::infinity())
                        {
                            const float weight = 1.0f / angle_dir_b;
                            weighted_sum += weight * rayhit.ray.tfar;
                            weight_sum += weight;
                        }
                    }
                }
                //we assign the result of the weighted average to the quality of the face
                inputM.face[i].Q() = (weighted_sum / weight_sum);

                if (cb && (((i + 1) % blockSize == 0) || (i == faceCount - 1))) {
                    const int pos = int((100.0 * double(i + 1)) / double(faceCount));
                    const std::string msg = vcg::StrFormat("Computing shape diameter (%d/%d)", i + 1, faceCount);
                    if (!(*cb)(pos, msg.c_str())) {
                        interrupted = true;
                        break;
                    }
                }
            }

            if (!interrupted)
                tri::UpdateColor<MeshType>::PerFaceQualityRamp(inputM);
            release_global_resources();
        }

        /*
        @Author: Paolo Fasano
        @Parameter: MeshType &m, reference to a mesh.
        @Parameter: int nRay, number of rays that must be generated and shoot.
        @Description: Given a mesh for each face, for each ray, it detects all the intersections with all the facets
            (i.e., without stopping at the first intersection), and accumulates the number.
            The rays are shoot two times each, one inside and one outside the mesh.
            After shooting all the rays, the facet is flipped if frontHit>backHit.

            For more informations read:
            Kenshi Takayama, Alec Jacobson, Ladislav Kavan, and Olga Sorkine-Hornung, A Simple Method for Correcting Facet Orientations in Polygon Meshes Based on Ray Casting, Journal of Computer Graphics Techniques (JCGT), vol. 3, no. 4, 53-63, 2014
            Available online http://jcgt.org/published/0003/04/02/
        */
        public:
         void computeNormalAnalysis(
             MeshType &inputM,
             int nRay,
             bool parity_computation,
             CallBackPos *cb = nullptr){

            if (cb && !(*cb)(0, "Analyzing face normals")) {
                release_global_resources();
                return;
            }

            std::vector<Point3f> unifDirVec;
            GenNormal<float>::Fibonacci(nRay,unifDirVec);

            std::vector<Point3f> unifDirVec_EXPAND;
            for (Point3f p : unifDirVec)
            {
                unifDirVec_EXPAND.push_back(p);
                unifDirVec_EXPAND.push_back(p*-1);
            }

            tri::UpdateSelection<MeshType>::FaceClear(inputM);


            bool completed = true;
            if (parity_computation){
                completed = paritySampling(inputM,unifDirVec_EXPAND, cb);
            }
            else{
                completed = visibilitySampling(inputM, unifDirVec_EXPAND, cb);
            }

            if (!completed)
                return;

            // Iterate over the selected faces and flip them
            for (auto& face : inputM.face)
            {
                if (face.IsS())
                {
                    std::swap(face.V(1), face.V(2));  // Swap the second and third vertices
                }
            }
            tri::UpdateSelection<MeshType>::FaceClear(inputM);
            if (cb)
                (*cb)(100, "Analyzing face normals");

        }


        /*
        @Author: Paolo Fasano
        @Parameter: Point3f origin, the origin point to search for intersections to
        @Description: given an origin point this methos counts how many intersection there are starting from there
            (only the number not the positions coordinates)
        */
        public:
         int findInterceptNumber(Point3f origin, Point3f direction){

            int totInterception = 0;

            RTCRayHit rayhit;
            rayhit = setRayValues(origin, direction, 0.5);
            RTCRayQueryContext context;
            rtcInitRayQueryContext(&context);

            RTCIntersectArguments intersectArgs;
            rtcInitIntersectArguments(&intersectArgs);
            intersectArgs.context = &context;

            while(true){
                rtcIntersect1(scene, &rayhit, &intersectArgs);
                if (rayhit.ray.tfar != std::numeric_limits<float>::infinity()){
                    totInterception++;
                    rayhit.ray.tnear += rayhit.ray.tfar;
                    rayhit.ray.tfar = std::numeric_limits<float>::infinity();
                }
                else
                    return totInterception;
            }
        }


        public:
            bool visibilitySampling(
                MeshType &inputM,
                std::vector<Point3f> unifDirVec_EXPAND,
                CallBackPos *cb = nullptr){
            const int faceCount = inputM.FN();
            const int blockSize = (cb != nullptr) ? std::max(1, faceCount / callbackChunkCount) : faceCount;
            bool interrupted = false;

            auto computeFace = [&](int i) {
                Point3f b = vcg::Barycenter(inputM.face[i]);

                int frontHit = 0;
                int backHit = 0;

                for(int r = 0; r<int(unifDirVec_EXPAND.size()); r++){
                    const Point3f dir = unifDirVec_EXPAND[r];
                    float scalarP = inputM.face[i].N()*dir;

                    RTCRayHit rayhit = setRayValues(b, dir, rayEpsilon);
                    RTCRayQueryContext context;
                    rtcInitRayQueryContext(&context);

                    RTCIntersectArguments intersectArgs;
                    rtcInitIntersectArguments(&intersectArgs);
                    intersectArgs.context = &context;

                    rtcIntersect1(scene, &rayhit, &intersectArgs);

                    if (rayhit.ray.tfar == std::numeric_limits<float>::infinity()) {
                        if (scalarP > 0)
                            frontHit++;
                        else
                            backHit++;
                    }
                }

                if(frontHit < backHit)
                    inputM.face[i].SetS();
            };

            for (int begin = 0; begin < faceCount && !interrupted; begin += blockSize) {
                const int end = std::min(faceCount, begin + blockSize);
                #pragma omp parallel for
                for(int i = begin; i < end; i++)
                    computeFace(i);

                if (cb) {
                    const int pos = int((100.0 * double(end)) / double(faceCount));
                    const std::string msg = vcg::StrFormat("Analyzing normals (visibility %d/%d)", end, faceCount);
                    if (!(*cb)(pos, msg.c_str()))
                        interrupted = true;
                }
            }

            release_global_resources();
            return !interrupted;
        }

        public:
        bool paritySampling(
            MeshType &inputM,
            std::vector<Point3f> unifDirVec_EXPAND,
            CallBackPos *cb = nullptr){

            const int faceCount = inputM.FN();
            const int blockSize = (cb != nullptr) ? std::max(1, faceCount / callbackChunkCount) : faceCount;
            bool interrupted = false;

            auto computeFace = [&](int i) {
                Point3f b = vcg::Barycenter(inputM.face[i]);

                int frontHit = 0;
                int backHit = 0;

                for(int r = 0; r<int(unifDirVec_EXPAND.size()); r++){
                    const Point3f dir = unifDirVec_EXPAND[r];
                    float scalarP = inputM.face[i].N()*dir;

                    RTCRayHit rayhit = setRayValues(b, dir, rayEpsilon);
                    RTCRayQueryContext context;
                    rtcInitRayQueryContext(&context);

                    RTCIntersectArguments intersectArgs;
                    rtcInitIntersectArguments(&intersectArgs);
                    intersectArgs.context = &context;

                    rtcIntersect1(scene, &rayhit, &intersectArgs);

                    if (rayhit.ray.tfar != std::numeric_limits<float>::infinity()) {
                        int n_hits = findInterceptNumber(b, dir);

                        if (scalarP > 0)
                            frontHit += (n_hits % 2);
                        else
                            backHit += (n_hits % 2);
                    }
                }

                if(frontHit > backHit)
                    inputM.face[i].SetS();
            };

            for (int begin = 0; begin < faceCount && !interrupted; begin += blockSize) {
                const int end = std::min(faceCount, begin + blockSize);
                #pragma omp parallel for
                for(int i = begin; i < end; i++)
                    computeFace(i);

                if (cb) {
                    const int pos = int((100.0 * double(end)) / double(faceCount));
                    const std::string msg = vcg::StrFormat("Analyzing normals (parity %d/%d)", end, faceCount);
                    if (!(*cb)(pos, msg.c_str()))
                        interrupted = true;
                }
            }

            release_global_resources();
            return !interrupted;
        }

        /*
            @Author: Paolo Fasano
            @Description:  
                This method shoots a single ray from origin toward direction.
                The return tuple is composed by 
                    - bool represents if the ray hit something
                    - point3f the coordinates at which the ray hit something
                    - float distance between point hit and the origin point
                    - int the id of the face hit by the ray
                If release_resources = true than the scene is deleted from memory after the ray is shoot, if you want to shoot multiple rays 
                    from the same scene (without the need to load the mesh again) set it to false

                    NOTE: 
                        The following is the RIGHT WAY  
                            EmbreeAdaptor<MyMesh> adaptor = EmbreeAdaptor<MyMesh>(mesh);
                            auto [hit_something, hit_face_coords, hit_distance, id] = adaptor.shoot_ray(origin, direction, false);
                            auto [hit_something, hit_face_coords, hit_distance, id] = adaptor.shoot_ray(origin2, direction2);
                        
                        In a loop
                            EmbreeAdaptor<MyMesh> adaptor = EmbreeAdaptor<MyMesh>(mesh);
                            for (int i = 0; i < origins.size(); i++){
                                auto [hit_something, hit_face_coords, hit_distance, id] = adaptor.shoot_ray(origins[i], direction[i], false);
                            }
                            adaptor.release_global_resources()
                            
                        The following code will create an ERROR
                            EmbreeAdaptor<MyMesh> adaptor = EmbreeAdaptor<MyMesh>(mesh);
                            auto [hit_something, hit_face_coords, hit_distance, id] = adaptor.shoot_ray(origin, direction);
                            auto [hit_something, hit_face_coords, hit_distance, id] = adaptor.shoot_ray(origin2, direction2);
                        
                        
        */
        public:
            inline std::tuple<bool, Point3f, float, int> shoot_ray(Point3f origin, Point3f direction, bool release_resources = true){
                return shoot_ray(origin, direction, rayEpsilon, release_resources);
            }

        public:
            inline std::tuple<bool, Point3f, float, int> shoot_ray(Point3f origin, Point3f direction, float tnear, bool release_resources = true){
                
                bool hit_something = false;
                Point3f hit_face_coords(0.0f, 0.0f, 0.0f);
                float hit_distance = 0;
                int hit_face_id = 0;

                RTCRayHit rayhit = setRayValues(origin, direction, tnear);

                RTCRayQueryContext context;
                rtcInitRayQueryContext(&context);

                RTCIntersectArguments intersectArgs;
                rtcInitIntersectArguments(&intersectArgs);
                intersectArgs.context = &context;

                rtcIntersect1(scene, &rayhit, &intersectArgs);

                if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID) {
                    hit_something = true;
                    hit_face_id = rayhit.hit.primID;
                    hit_distance = rayhit.ray.tfar;
                    hit_face_coords = origin + direction * (hit_distance / direction.Norm());
                }
                else {
                    hit_face_id = rayhit.hit.primID;
                    hit_distance = rayhit.ray.tfar;
                }
                
                if(release_resources){
                    release_global_resources();
                }

                return std::make_tuple(hit_something, hit_face_coords, hit_distance, hit_face_id);
            }


        public:
            void release_global_resources() {
                if (scene)  { rtcReleaseScene(scene);  scene  = nullptr; }
                if (device) { rtcReleaseDevice(device); device = nullptr; }
            }


        /*
            @Author: Paolo Fasano
            @Parameter: MeshType &inputM, the the input mesh
            @Parameter: std::vector<Point3f> origins, the vector of Point3f (coordinates) where the vertex are created (if use_edge is true it represent the origin of the edge as well)
            @Parameter: std::vector<Point3f> directions, the vector of Point3f (coordinates) where the vertex are created (if use_edge is true it represent the direction of the edge as well)
            @Parameter: bool add_edge, if true Edges are generated between origins[i] and directions[i] 
            @Description: given a vector of origins and directions, creates a vertex for each origin and each direction;
                            if add_edges is true than it creates a edge between the i-th origin and the i-th direction.
                            note: to save and visualize a edge you must save as follows

                                    //your edge declaration must contain the following 
                                    class MyEdge : public vcg::Edge<MyUsedTypes, vcg::edge::VertexRef, vcg::edge::BitFlags> {};

                                    //your code here
                                    
                                    int mask = vcg::tri::io::Mask::IOM_VERTCOORD;
                                    mask |= vcg::tri::io::Mask::IOM_EDGEINDEX;
                                    tri::io::ExporterPLY<MyMesh>::Save(YOUR_MESH, "EdgeTest.ply", mask);
                
                I SUGGEST TO USE ANY OF THE visualize_ray_shoot ON A EMPTY MESH 
        */
        public:
            inline void visualize_ray_shoot(MeshType &inputM, std::vector<Point3f> origins , std::vector<Point3f> directions, bool add_edge = false){                
                if (origins.size() == directions.size()){
                    for (int i = 0; i < origins.size(); i++){
                        visualize_ray_shoot(inputM, origins[i], directions[i], add_edge);
                    }
                }
                else
                    std::cout<<"Error executing visualize_ray_shoot: std::vector<Point3f> origins and std::vector<Point3f> directions must have same size"<<std::endl;
                
            }
        public:
            inline void visualize_ray_shoot(MeshType &inputM, Point3f origin , Point3f direction, bool add_edge = false){                
                    
                if (add_edge){
                    tri::Allocator<MeshType>::AddEdge(inputM, origin, direction);                
                } 
                else{
                    tri::Allocator<MeshType>::AddVertex(inputM, origin);                
                    tri::Allocator<MeshType>::AddVertex(inputM, direction);      
                }         
            }
        public:
            inline void visualize_ray_shoot(MeshType &inputM, Point3f origin , std::vector<Point3f> directions, bool add_edge = false){                

                tri::Allocator<MeshType>::AddVertex(inputM, origin);                

                for (int i = 0; i < directions.size(); i++){
                    
                    tri::Allocator<MeshType>::AddVertex(inputM, directions[i]);      

                    if (add_edge){
                        tri::Allocator<MeshType>::AddEdge(inputM, &inputM.vert[inputM.vert.size()-i-2], &inputM.vert[inputM.vert.size()-1]);                
                    } 
                }
            }


        public:
            void print_ray_informations(RTCRayHit rayhit){

                std::cout<< "origin of ray " << rayhit.ray.org_x<< " " << rayhit.ray.org_y<< " " << rayhit.ray.org_z<< " " <<std::endl;
                std::cout<< "direction of ray " << rayhit.ray.dir_x<< " " << rayhit.ray.dir_y<< " " << rayhit.ray.dir_z<< " " <<std::endl;
                std::cout<< "tnear " << rayhit.ray.tnear <<std::endl;
                std::cout<< "tfar " << rayhit.ray.tfar <<std::endl;
            }


        public:
            inline RTCRayHit initRayValues(){
                RTCRayHit rayhit;
                rayhit.ray.mask   = -1;
                rayhit.ray.flags = 0;
                rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
                rayhit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
                return rayhit;
            }

        //given a ray and a direction expressed as point3f, this method modifies the ray direction of the ray tp the given direction
        public:
            inline void updateRayDirection(RTCRayHit& rayhit, Point3f direction){

                //setting the ray direction
                rayhit.ray.dir_x = direction[0];
                rayhit.ray.dir_y = direction[1];
                rayhit.ray.dir_z = direction[2];
            }


        //given a ray and a point of origin expressed as point3f, this method modifies the origin point of the ray tp the origin point given
        public:
            inline void updateRayOrigin(RTCRayHit& rayhit, Point3f origin){

                //setting the ray point of origin
                rayhit.ray.org_x = origin[0];
                rayhit.ray.org_y = origin[1];
                rayhit.ray.org_z = origin[2];
            }

        public:
            inline RTCRayHit setRayValues(Point3f origin, Point3f direction, float tnear, float tfar = std::numeric_limits<float>::infinity()){

                RTCRayHit rayhit = initRayValues();

                updateRayOrigin(rayhit, origin);
                updateRayDirection(rayhit, direction);
                rayhit.ray.tnear  = tnear;
                rayhit.ray.tfar   = tfar;

                return rayhit;
            }
    };
}
#endif
