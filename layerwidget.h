#pragma once

#include <QTreeWidget>

class Document;

class LayerWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit LayerWidget(Document *doc, QWidget *parent = nullptr);

private:
    void rebuild();
    Document *m_doc;
};
