#pragma once

#include "document.h"

#include <QString>
#include <QStringList>

// Presentation policy shared by every surface that lists or describes filters —
// the filter panel and the Filter Plugins Info dialog. Kept in one place so the two
// cannot drift: they previously carried near-identical search predicates, and the
// panel showed raw `outputModifies` codes while the dialog spelled them out.
namespace FilterPresentation {

// Split a query into lower-cased, de-duplicated terms. All terms must match.
QStringList tokenize(const QString &text);

// True if every term is found somewhere in the filter's searchable text: name,
// Python name, category, plugin, descriptions and upstream project.
bool matches(const Document::FilterInfo &info, const QStringList &terms);

// True if every term is in the display name alone. Used for ranking, so that
// name matches sort above matches found only in a category or description.
bool titleMatchesAll(const Document::FilterInfo &info, const QStringList &terms);

// Human-readable expansion of the two-letter `outputModifies` codes, e.g.
// {"FQ","VQ"} -> {"face scalar", "vertex scalar"}. Unknown codes pass through.
QStringList modifiedDataLabels(const QStringList &codes);

} // namespace FilterPresentation
