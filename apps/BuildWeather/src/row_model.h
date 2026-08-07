#pragma once

// A tiny list model over pre-formatted rows.
//
// Every table in the analysis tab is "a fixed set of named columns, already
// turned into display strings". Formatting once when the data changes rather
// than once per delegate binding keeps scrolling a ten-thousand-row header
// ranking smooth, and keeps number formatting out of QML.

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace BW::UI
{

class RowModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by App")
    Q_PROPERTY(int count READ rowCountProperty NOTIFY countChanged)

public:
    explicit RowModel(QStringList keys = {}, QObject *parent = nullptr);

    void setKeys(QStringList keys);

    /// `rows` holds one QVariantList per row, in the same order as the keys.
    void setRows(QList<QVariantList> rows);

    void clear();

    [[nodiscard]]
    auto rowCount(const QModelIndex &parent = {}) const -> int override;

    [[nodiscard]]
    auto data(const QModelIndex &index, int role) const -> QVariant override;

    [[nodiscard]]
    auto roleNames() const -> QHash<int, QByteArray> override;

    [[nodiscard]]
    auto rowCountProperty() const -> int
    {
        return static_cast<int>(m_rows.size());
    }

    Q_INVOKABLE QVariant value(int row, const QString &key) const;

Q_SIGNALS:
    void countChanged();

private:
    QStringList m_keys;
    QList<QVariantList> m_rows;
};

}
