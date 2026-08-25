#pragma once

#include <QString>

class Document;
class FilterParams;
struct MeshFilterRunResult;

bool isIglQueryFilter(const QString &filterId);
MeshFilterRunResult runIglQueryFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc);
