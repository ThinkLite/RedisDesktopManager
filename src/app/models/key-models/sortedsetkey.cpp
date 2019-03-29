#include "sortedsetkey.h"
#include <qredisclient/connection.h>

SortedSetKeyModel::SortedSetKeyModel(
    QSharedPointer<RedisClient::Connection> connection, QByteArray fullPath,
    int dbIndex, long long ttl)
    : KeyModel(connection, fullPath, dbIndex, ttl, "ZCARD",
               "ZRANGE WITHSCORES") {}

QString SortedSetKeyModel::type() { return "zset"; }

QStringList SortedSetKeyModel::getColumnNames() {
  return QStringList() << "row"
                       << "value"
                       << "score";
}

QHash<int, QByteArray> SortedSetKeyModel::getRoles() {
  QHash<int, QByteArray> roles;
  roles[Roles::RowNumber] = "row";
  roles[Roles::Value] = "value";
  roles[Roles::Score] = "score";
  return roles;
}

QVariant SortedSetKeyModel::getData(int rowIndex, int dataRole) {
  if (!isRowLoaded(rowIndex)) return QVariant();

  QPair<QByteArray, QByteArray> row = m_rowsCache[rowIndex];

  if (dataRole == Roles::Value)
    return row.first;
  else if (dataRole == Roles::Score)
    return row.second.toDouble();
  else if (dataRole == Roles::RowNumber)
    return rowIndex + 1;

  return QVariant();
}

void SortedSetKeyModel::updateRow(int rowIndex, const QVariantMap &row,
                                  Callback c) {
  if (!isRowLoaded(rowIndex) || !isRowValid(row)) {
    emit m_notifier->error(QCoreApplication::translate("RDM", "Invalid row"));
    return;
  }

  QPair<QByteArray, QByteArray> cachedRow = m_rowsCache[rowIndex];

  bool valueChanged = cachedRow.first != row["value"].toString();
  bool scoreChanged = cachedRow.second != row["score"].toString();

  QPair<QByteArray, QByteArray> newRow(
      (valueChanged) ? row["value"].toByteArray() : cachedRow.first,
      (scoreChanged) ? row["score"].toByteArray() : cachedRow.second);

  auto onRowAdded = [this, c, rowIndex, newRow](const QString &err) {
    if (err.isEmpty()) m_rowsCache.replace(rowIndex, newRow);

    return c(err);
  };

  if (valueChanged) {
    deleteSortedSetRow(
        cachedRow.first, [this, c, onRowAdded, newRow](const QString &err) {
          if (err.size() > 0) return c(err);

          addSortedSetRow(newRow.first, newRow.second, onRowAdded, true);
        });
  } else {
    addSortedSetRow(newRow.first, newRow.second, onRowAdded, true);
  }
}

void SortedSetKeyModel::addRow(const QVariantMap &row, Callback c) {
  if (!isRowValid(row)) {
    return c(QCoreApplication::translate("RDM", "Invalid row"));
  }

  QPair<QByteArray, QByteArray> cachedRow(row["value"].toByteArray(),
                                          row["score"].toByteArray());

  auto onAdded = [this, c, cachedRow](const QString &err) {
    if (err.isEmpty()) {
      m_rowsCache.push_back(cachedRow);
      m_rowCount++;
    }

    return c(err);
  };

  addSortedSetRow(cachedRow.first, cachedRow.second, onAdded);
}

void SortedSetKeyModel::removeRow(int i, Callback c) {
  if (!isRowLoaded(i)) return;

  QByteArray value = m_rowsCache[i].first;

  executeCmd({"ZREM", m_keyFullPath, value}, [this, c, i](const QString &err) {
    if (err.isEmpty()) {
      m_rowCount--;
      m_rowsCache.removeAt(i);
      setRemovedIfEmpty();
    }

    return c(err);
  });
}

void SortedSetKeyModel::addSortedSetRow(const QByteArray &value,
                                        QByteArray score, Callback c,
                                        bool updateExisting) {
  QList<QByteArray> cmd;

  if (updateExisting) {
    cmd = {"ZADD", "XX", m_keyFullPath, score, value};
  } else {
    cmd = {"ZADD", m_keyFullPath, score, value};
  }

  executeCmd(cmd, c, CmdHandler(), RedisClient::Response::Integer);
}

void SortedSetKeyModel::deleteSortedSetRow(const QByteArray &value,
                                           Callback c) {
  executeCmd({"ZREM", m_keyFullPath, value}, c);
}

void SortedSetKeyModel::addLoadedRowsToCache(const QVariantList &rows,
                                             QVariant rowStartId) {
  QList<QPair<QByteArray, QByteArray>> result;

  for (QVariantList::const_iterator item = rows.begin(); item != rows.end();
       ++item) {
    QPair<QByteArray, QByteArray> value;
    value.first = item->toByteArray();
    ++item;

    if (item == rows.end()) {
      emit m_notifier->error(QCoreApplication::translate(
          "RDM", "Data was loaded from server partially."));
      return;
    }

    value.second = item->toByteArray();
    result.push_back(value);
  }

  auto rowStart = rowStartId.toULongLong();
  m_rowsCache.addLoadedRange({rowStart, rowStart + result.size() - 1}, result);
}
