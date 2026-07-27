#pragma once

#include <QDialog>
#include <QJsonArray>

class QTableWidget;

class VesselPathSelectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VesselPathSelectionDialog(
        const QJsonArray &candidatePaths,
        QWidget *parent = nullptr);

    QString selectedPathId() const;

private:
    QTableWidget *m_table = nullptr;
};
