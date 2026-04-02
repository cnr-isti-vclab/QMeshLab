#pragma once

#include <QTreeWidget>

class Document;

class MeshTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit MeshTreeWidget(Document *doc, QWidget *parent = nullptr);

private:
    void rebuild();
    Document *m_doc;
};
