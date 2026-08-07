#include "row_model.h"

namespace BW::UI
{

RowModel::RowModel(QStringList keys, QObject *parent)
    : QAbstractListModel { parent }
    , m_keys { std::move(keys) }
{
}

void RowModel::setKeys(QStringList keys)
{
    beginResetModel();
    m_keys = std::move(keys);
    m_rows.clear();
    endResetModel();
    Q_EMIT countChanged();
}

void RowModel::setRows(QList<QVariantList> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    Q_EMIT countChanged();
}

void RowModel::clear()
{
    if (m_rows.isEmpty()) {
        return;
    }
    beginResetModel();
    m_rows.clear();
    endResetModel();
    Q_EMIT countChanged();
}

auto RowModel::rowCount(const QModelIndex &parent) const -> int
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

auto RowModel::data(const QModelIndex &index, int role) const -> QVariant
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const int column = role - Qt::UserRole;
    const QVariantList &row = m_rows[index.row()];
    if (column < 0 || column >= row.size()) {
        return {};
    }
    return row[column];
}

auto RowModel::roleNames() const -> QHash<int, QByteArray>
{
    QHash<int, QByteArray> names;
    for (int i = 0; i < m_keys.size(); ++i) {
        names.insert(Qt::UserRole + i, m_keys[i].toUtf8());
    }
    return names;
}

auto RowModel::value(int row, const QString &key) const -> QVariant
{
    const int column = m_keys.indexOf(key);
    if (row < 0 || row >= m_rows.size() || column < 0
        || column >= m_rows[row].size()) {
        return {};
    }
    return m_rows[row][column];
}

}
