# SqliteChunkRetriever
Qt5 library with which you can retrieve deleted records chunk of Sqlite3 databases

# example
```
SqliteChunkRetriever sqliteDb;
if (sqliteDb.open("C:/users/gab/Desktop/mmssms.db"))
    return;

QVector<SqliteChunkRetriever::SqliteTable> tables = sqliteDb.getMasterTable();

for (SqliteChunkRetriever::SqliteTable &t: tables) {
    if(t.type != SqliteChunkRetriever::TableType::Table)
        continue;

    if (t.tbl_name == "sms") {
        QVector<QByteArray> unalloc = sqliteDb.getUnallocatedOfTable(t);
        foreach (QByteArray binary, unalloc) {
            qDebug()<<binary;
        }
    }
}
sqliteDb.close();
```
