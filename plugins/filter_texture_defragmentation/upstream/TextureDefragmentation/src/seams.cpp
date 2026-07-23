/*******************************************************************************
    Copyright (c) 2021, Andrea Maggiordomo, Paolo Cignoni and Marco Tarini

    This file is part of TextureDefrag, a reference implementation for
    the paper ``Texture Defragmentation for Photo-Reconstructed 3D Models''
    by Andrea Maggiordomo, Paolo Cignoni and Marco Tarini.

    TextureDefrag is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    TextureDefrag is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with TextureDefrag. If not, see <https://www.gnu.org/licenses/>.
*******************************************************************************/

#include "seams.h"
#include "mesh_attribute.h"
#include "logging.h"

#include <vcg/complex/algorithms/clean.h>


static void SortSeam(SeamHandle seam);
static int NextNotVisitedEdge(const SeamMesh& sm, const std::vector<int>& edges);

static inline PosF GetDualPos(Mesh& m, const PosF& pos, Mesh::PerFaceAttributeHandle<FF>& ffadj);


ChartPair GetCharts(ClusteredSeamHandle csh, GraphHandle graph, bool *swapped)
{
    ensure(csh->size() > 0);

    if (swapped)
        *swapped = false;

    SeamMesh& sm = csh->sm;
    SeamEdge se = sm.edge[csh->seams[0]->edges[0]];
    ChartPair p(graph->GetChart(se.fa->id), graph->GetChart(se.fb->id));

    if (p.first->FN() < p.second->FN()) {
        std::swap(p.first, p.second);
        if (swapped)
            *swapped = true;
    }

    return p;
}

std::set<int> GetEndpoints(ClusteredSeamHandle csh)
{
    // count occurrences and extract real endpoints
    std::map<int, int> endpoints;
    for (SeamHandle sh : csh->seams) {
        for (int e : sh->endpoints)
            endpoints[e]++;
    }

    // only return endpoints found once
    std::set<int> es;
    for (auto entry : endpoints)
        if (entry.second == 1)
            es.insert(entry.first);

    return es;
}

void ColorizeSeam(ClusteredSeamHandle csh, const vcg::Color4b& color)
{
    for (auto sh : csh->seams)
        ColorizeSeam(sh, color);
}

void ColorizeSeam(SeamHandle sh, const vcg::Color4b& color)
{
    SeamMesh& sm = sh->sm;
    for (int e : sh->edges) {
        sm.edge[e].fa->C() = color;
        sm.edge[e].fb->C() = color;
    }
}

double ComputeSeamLength3D(ClusteredSeamHandle csh)
{
    ensure(csh->size() > 0);
    double l = 0;
    for (auto sh : csh->seams)
        l += ComputeSeamLength3D(sh);
    return l;
}

double ComputeSeamLength3D(SeamHandle sh)
{
    double l = 0;
    SeamMesh& sm = sh->sm;
    for (int e : sh->edges)
        l += (sm.edge[e].V(0)->P() - sm.edge[e].V(1)->P()).Norm();
    return l;
}

// ASSUMPTION: the mesh is coherently oriented in 3D and UV space
void ExtractUVCoordinates(ClusteredSeamHandle csh, std::vector<Point2d>& uva, std::vector<Point2d>& uvb, const std::unordered_set<RegionID> &a)
{
    std::unordered_set<Mesh::VertexPointer> visited;
    for (SeamHandle sh : csh->seams) {
        SeamMesh& seamMesh = sh->sm;
        for (int iedge : sh->edges) {
            SeamEdge& edge = seamMesh.edge[iedge];
            Mesh::FacePointer fa = edge.fa;
            Mesh::FacePointer fb = edge.fb;
            int ea = edge.ea;
            int eb = edge.eb;
            if (a.find(edge.fa->id) == a.end()) {
                std::swap(fa, fb);
                std::swap(ea, eb);
            }
            if ((visited.count(fa->V0(ea)) == 0) || (visited.count(fb->V1(eb)) == 0)) {
                visited.insert(fa->V0(ea));
                visited.insert(fb->V1(eb));
                uva.push_back(fa->V0(ea)->T().P());
                uvb.push_back(fb->V1(eb)->T().P());
            }
            if ((visited.count(fa->V1(ea)) == 0) || (visited.count(fb->V0(eb)) == 0)) {
                visited.insert(fa->V1(ea));
                visited.insert(fb->V0(eb));
                uva.push_back(fa->V1(ea)->T().P());
                uvb.push_back(fb->V0(eb)->T().P());
            }
        }
    }
}

void BuildSeamMesh(Mesh& m, SeamMesh& seamMesh, GraphHandle graph)
{
    seamMesh.Clear();

    auto ffadj = Get3DFaceAdjacencyAttribute(m);

    seamMesh.Clear();
    tri::UpdateFlags<Mesh>::FaceClearFaceEdgeS(m);
    for (auto& f : m.face) {
        for (int i = 0; i < 3; ++i) {

            // An edge is a seam edge if it is both manifold in the raw 3D mesh, and on the border of a chart in the
            // UV parametrization. Then we need to check if the seam is in a single chart, or it belongs to two charts.
            //
            // To avoid adding the same edge twice (we will see it twice, once per chart), we mark its occurrence on
            // all charts it appears as SELECTED.
            if (IsEdgeManifold3D(m, f, i, ffadj) && face::IsBorder(f, i) && f.IsFaceEdgeS(i) == false) {
                PosF pa(&f, i);
                PosF pb = GetDualPos(m, pa, ffadj);
                ChartHandle ca = graph->GetChart(pa.F()->id);
                ChartHandle cb = graph->GetChart(pb.F()->id);
                if (ca == cb || ca->adj.count(cb) > 0) {

                    // Forces canonical order.
                    if (pa.F()->id > pb.F()->id)
                        std::swap(pa, pb);
                    auto ei = tri::Allocator<SeamMesh>::AddEdge(seamMesh, pa.V()->P(), pa.VFlip()->P());
                    ei->fa = pa.F();
                    ei->ea = pa.E();
                    ei->fb = pb.F();
                    ei->eb = pb.E();
                    pa.F()->SetFaceEdgeS(pa.E());
                    pb.F()->SetFaceEdgeS(pb.E());
                }
            }
        }
    }

    // Clean the constructed instance and construct its topology.
    tri::Clean<SeamMesh>::RemoveDuplicateVertex(seamMesh);
    tri::UpdateTopology<SeamMesh>::VertexEdge(seamMesh);
    tri::UpdateTopology<SeamMesh>::EdgeEdge(seamMesh);
}

// seams are sorted according to the edge mesh topology, and kept sorted when
// merging them into clusters. This simplifies things if the seam later needs
// to be shortened
std::vector<SeamHandle> GenerateSeams(SeamMesh& seamMesh)
{
    // Initialization.
    // For consistency, we clear all vertices and edges VISITED marking.
    // For edges the marking will be used to determine if one has already been assigned to a chain or not.
    // For vertices, we do not use the VISITED marking.
    std::vector<SeamHandle> svec;
    tri::UpdateFlags<SeamMesh>::VertexClearV(seamMesh);
    tri::UpdateFlags<SeamMesh>::EdgeClearV(seamMesh);

    // We scan all the seams by visiting the edge star of every vertex.
    // For each unvisited edge (that is not degenerate) we build a new seam chain. A chain is associated with the pair of
    // charts sharing the seed edge. We construct the chain via Edge-Edge adjacency, checking for the current edge
    // its endpoints, determining if they can be used to continue the chain or not.
    for (auto& v : seamMesh.vert) {
        std::vector<SeamMesh::EdgePointer> starVec;
        edge::VEStarVE(&v, starVec);
        for (auto startEdge : starVec) {

            // If the current seam has been visited, it means it already belongs to a chain.
            // If we have the same face on both sides of the seam, then the edge is degenerate and must be excluded.
            if (startEdge->IsV() || (startEdge->fa == startEdge->fb)) {
                continue;
            }

            std::pair<RegionID, RegionID> chartPair = std::make_pair(startEdge->fa->id, startEdge->fb->id);

            // We construct a chain via Depth-First-Search using a stack. At each step we pop from the stack the
            // current candidate, add it to the chain and use it to found new ones. Where to expand the chain is
            // determined by the extremes of the current seam.
            // A vertex is deemed valid to be expanded if these two properties hold:
            //  - If the vertex has degree equal to two, meaning its incident edges are only the current one and the
            //    new candidate.
            //  - If all edges in its star belong to the same chart pair as the chain.
            // Otherwise, the vertex is considered an endpoint.
            SeamHandle seam = std::make_shared<Seam>(seamMesh);
            startEdge->SetV();
            std::stack<SeamMesh::EdgePointer> s;
            s.push(startEdge);
            while (!s.empty()) {
                SeamMesh::EdgePointer eptr = s.top();
                s.pop();
                seam->edges.push_back(tri::Index(seamMesh, eptr));
                for (int i = 0; i < 2; ++i) {
                    std::vector<SeamMesh::EdgePointer> eptrStarVec;
                    edge::VEStarVE(eptr->V(i), eptrStarVec);

                    // test edge case where the seam traverses a non-manif vert adjacent to multiple charts
                    bool nonManifoldVertexOnSeam = false;
                    for (auto ep : eptrStarVec) {
                        bool sameCharts = std::make_pair(ep->fa->id, ep->fb->id) == chartPair;
                        bool sameChartsRev = std::make_pair(ep->fb->id, ep->fa->id) == chartPair;
                        nonManifoldVertexOnSeam |= !(sameCharts || sameChartsRev);
                    }

                    if (eptrStarVec.size() != 2 || nonManifoldVertexOnSeam) {
                        seam->endpoints.push_back(tri::Index(seamMesh, eptr->V(i)));
                    } else if(eptr->EEp(i)->IsV() == false) {
                        eptr->EEp(i)->SetV();
                        s.push(eptr->EEp(i));
                    }
                }
            }

            // Ensure correctness for the current produced chart, checking that is closed.
            // The chart must have two endpoint vertices. If it has one, then a bug occurred.
            // If it has zero, it means the chart defines a closed loop, with the traversal ended after reaching the
            // starting seam. We need then to arbitrarily pick one vertex, considering twice as the endpoints.
            ensure(seam->endpoints.size() != 1);
            if (seam->endpoints.size() == 0) {
                seam->endpoints.push_back(tri::Index(seamMesh, startEdge->V(0)));
                seam->endpoints.push_back(tri::Index(seamMesh, startEdge->V(0)));
            }

            // Sort the edges in the produced chart to be in sequential order, moving from one endpoint to the other.
            SortSeam(seam);
            svec.push_back(seam);
        }
    }

    // Final correctness test: check if all seam edges belong to a chain.
    int nmissed = 0;
    for (auto& e : seamMesh.edge) {
        if (!(e.IsV() || (e.fa == e.fb))) {
            nmissed++;
        }
    }
    if (nmissed > 0)
        LOG_ERR << "Missed " << nmissed << " edges";
    ensure(nmissed == 0);
    return svec;
}

std::vector<ClusteredSeamHandle> ClusterSeamsByChartId(const std::vector<SeamHandle>& seams)
{

    // Defining the two main data structures for the algorithm.
    //
    // - `cshvec`: the output vector of clustered seams. Each entry groups all seam chains
    //   that share the same chart pair. Entries appear in the order their chart pair is
    //   first encountered during the iteration.
    //
    // - `cshmap`: maps each chart pair to its corresponding entry in `cshvec`.
    std::vector<ClusteredSeamHandle> cshvec;
    std::map<std::pair<RegionID, RegionID>, ClusteredSeamHandle> cshmap;

    // We construct both data structure by visiting the set of connected seams provided as parameter.
    // At each step we check the pair of charts associated to the current connected group and update the mapping and
    // vector accordingly.
    for (auto& sh : seams) {
        SeamMesh& sm = sh->sm;
        SeamEdge e = sm.edge[sh->edges.front()];
        std::pair<RegionID, RegionID> idpair(e.fa->id, e.fb->id);

        // Self-cut seams (both sides refer to the same chart) are stored as distinct entries
        // in `cshvec`. They are intentionally excluded from `cshmap` because two self-cuts
        // on the same chart are geometrically independent and should not be merged.
        if (idpair.first == idpair.second) {
            ClusteredSeamHandle csh = std::make_shared<ClusteredSeam>(sm);
            csh->seams.push_back(sh);
            cshvec.push_back(csh);
        }

        // For seams separating two distinct charts, `cshmap` is used to find or create the
        // ClusteredSeam entry for this chart pair. If no entry exists yet, a new one is
        // created and pushed into `cshvec` so both data structures stay in sync.
        // Either way, the current seam chain is appended to the entry's seam list.
        else {

            // Normalize the chart pair so the lower ID always comes first. This matches the
            // canonical ordering enforced in `BuildSeamMesh`, and ensures that (A, B) and (B, A)
            // — which represent the same seam boundary seen from opposite sides — map to the
            // same `cshmap` entry.
            if (idpair.first > idpair.second)
                std::swap(idpair.first, idpair.second);
            if (cshmap.find(idpair) == cshmap.end()) {
                cshmap[idpair] = std::make_shared<ClusteredSeam>(sm);
                cshvec.push_back(cshmap[idpair]);
            }
            cshmap[idpair]->seams.push_back(sh);
        }
    }

    return cshvec;
}

ClusteredSeamHandle Flatten(const std::vector<ClusteredSeamHandle>& cshVec)
{
    if (cshVec.size() == 0)
        return nullptr;

    ClusteredSeamHandle out = std::make_shared<ClusteredSeam>(cshVec.front()->sm);
    for (ClusteredSeamHandle csh : cshVec)
        for (SeamHandle sh : csh->seams)
            out->seams.push_back(sh);

    return out;
}

// -- static functions ---------------------------------------------------------


static void SortSeam(SeamHandle sh)
{
    if (sh->endpoints.size() == 0)
        return;

    SeamMesh& sm = sh->sm;

    std::map<int, std::vector<int>> vtoe; // vertex to edge map
    for (auto e : sh->edges) {
        vtoe[tri::Index(sm, sm.edge[e].V(0))].push_back(e);
        vtoe[tri::Index(sm, sm.edge[e].V(1))].push_back(e);
        sm.edge[e].ClearV();
    }

    std::vector<int> sortedEdges;
    int v = sh->endpoints.front();
    while (sortedEdges.size() < sh->edges.size()) {
        int e = NextNotVisitedEdge(sm, vtoe[v]);
        ensure(e != -1);
        sm.edge[e].SetV();
        sortedEdges.push_back(e);
        int nextv = tri::Index(sm, sm.edge[e].V(0));
        if (nextv == v)
            nextv = tri::Index(sm, sm.edge[e].V(1));
        v = nextv;
    }

    sh->edges = sortedEdges;
}

static int NextNotVisitedEdge(const SeamMesh& sm, const std::vector<int>& edges)
{
    for (auto e : edges)
        if (sm.edge[e].IsV() == false)
            return e;
    return -1;
}

static inline PosF GetDualPos(Mesh& m, const PosF& pos, Mesh::PerFaceAttributeHandle<FF>& ffadj)
{
    Mesh::FacePointer dualf = &m.face[ffadj[pos.F()].f[pos.E()]];
    int e = ffadj[pos.F()].e[pos.E()];
    PosF dual(dualf, e);
    dual.FlipV();
    return dual;
}
