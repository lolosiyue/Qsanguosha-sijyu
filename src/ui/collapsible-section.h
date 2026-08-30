#ifndef _COLLAPSIBLE_SECTION_H
#define _COLLAPSIBLE_SECTION_H

#include <QList>
#include <QWidget>

class QCheckBox;
class QGridLayout;
class QToolButton;

class CollapsibleSection : public QWidget
{
    Q_OBJECT

public:
    explicit CollapsibleSection(const QString &title, QWidget *parent = nullptr);

    void addPackageCheckbox(QCheckBox *checkbox);
    bool isEmpty() const { return m_checkboxes.isEmpty(); }

private slots:
    void onToggled(bool expanded);
    void selectAll();
    void deselectAll();
    void reverseSelect();
    void updateCount();

private:
    QString m_title;
    QToolButton *m_header;
    QWidget *m_content;
    QGridLayout *m_grid;
    QList<QCheckBox *> m_checkboxes;
};

#endif
