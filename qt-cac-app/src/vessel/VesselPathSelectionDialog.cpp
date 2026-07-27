#include "vessel/VesselPathSelectionDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

VesselPathSelectionDialog::VesselPathSelectionDialog(
    const QJsonArray &candidatePaths,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Select Vessel Path"));
    setModal(true);
    resize(780, 360);

    auto *layout = new QVBoxLayout(this);
    auto *explanation = new QLabel(
        QStringLiteral(
            "The selected component contains multiple meaningful centerline "
            "paths. Select one path; no anatomical name is inferred."),
        this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    m_table = new QTableWidget(candidatePaths.size(), 6, this);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Path"),
        QStringLiteral("Length (mm)"),
        QStringLiteral("Mean radius"),
        QStringLiteral("Minimum radius"),
        QStringLiteral("Maximum radius"),
        QStringLiteral("Points")
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    for (int row = 0; row < candidatePaths.size(); ++row) {
        const QJsonObject candidate = candidatePaths.at(row).toObject();
        const QString pathId =
            candidate.value(QStringLiteral("path_id")).toString();
        auto *pathItem = new QTableWidgetItem(pathId);
        pathItem->setData(Qt::UserRole, pathId);
        m_table->setItem(row, 0, pathItem);
        m_table->setItem(
            row, 1,
            new QTableWidgetItem(QString::number(
                candidate.value(QStringLiteral("physical_length_mm")).toDouble(),
                'f', 1)));
        for (int column = 2; column <= 4; ++column) {
            static const char *keys[] = {
                "mean_radius_mm",
                "minimum_radius_mm",
                "maximum_radius_mm"
            };
            const QJsonValue value =
                candidate.value(QString::fromLatin1(keys[column - 2]));
            m_table->setItem(
                row, column,
                new QTableWidgetItem(
                    value.isDouble()
                        ? QString::number(value.toDouble(), 'f', 2)
                        : QStringLiteral("\u2014")));
        }
        m_table->setItem(
            row, 5,
            new QTableWidgetItem(QString::number(
                candidate.value(QStringLiteral("point_count")).toInt())));
    }
    m_table->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    if (m_table->rowCount() > 0) {
        m_table->selectRow(0);
    }
    layout->addWidget(m_table, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString VesselPathSelectionDialog::selectedPathId() const
{
    if (!m_table || m_table->currentRow() < 0) {
        return {};
    }
    QTableWidgetItem *item =
        m_table->item(m_table->currentRow(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}
