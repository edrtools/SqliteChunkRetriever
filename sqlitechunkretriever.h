#ifndef SQLITECHUNKRETRIEVER_H
#define SQLITECHUNKRETRIEVER_H

#include "sqlitechunkretriever_global.h"

#include <QString>
#include <QFile>
#include <QByteArray>
#include <QtEndian>
#include <QDebug>
#include <QMap>
#include <QTextCodec>

class SQLITECHUNKRETRIEVERSHARED_EXPORT SqliteChunkRetriever
{
public:
    enum class TableType {
        Table,
        Index,
        View,
        Trigger,
        Unalloc
    };

    /**
     * page header
     */
    class PageHeader {
    public:
        PageHeader() {
            flag = 0;
            freeblock_offset = 0;
            num_cells = 0;
            cell_offset = 0;
            num_free_bytes = 0;
            right_most_pointer = 0;
        }

        quint8 flag;
        quint16 freeblock_offset;
        quint16 num_cells;
        quint32 cell_offset;                // it's a quint16 but if 0 => 65536
        quint8 num_free_bytes;
        quint32 right_most_pointer;
    };

    /**
     * table
     */
    class SqliteTable {
    public:
        SqliteTable() {
            type = TableType::Unalloc;
            rootpage = -1;
        }

        TableType type;           // table, index, view, trigger + unalloc
        QString name;           // object name
        QString tbl_name;       // table name, for a table or view, the tbl_name column is a copy of the name column
        qint32 rootpage;        // page number of the root b-tree page for tables and indexes
        QString sql;            // SQL text that describes the object
        QVector<qint32> child;  // array of leaves pages
    };

    SqliteChunkRetriever();
    ~SqliteChunkRetriever();

    /**
     * open database
     * @param path QString sqlite 3 database
     * @retval int 0 success otherwise fail
     */
    int open(QString path);

    /**
     * close database
     */
    void close();

    /**
     * retry all database tables
     * @retval QVector<SqliteRecovery::SqliteTable>
     */
    QVector<SqliteTable> getMasterTable();

    /**
     * retry unallocated buffers of all tables
     * @param SqliteTable table to recover
     * @retval QVector<QByteArray> of unallocated chunks
     */
    QVector<QByteArray> getUnallocatedOfTable(SqliteTable table);

    /**
     * retry unallocated buffers of a specified page
     * @param qint32 page to recover
     * @retval QVector<QByteArray> of unallocated chunks
     */
    QVector<QByteArray> getUnallocatedOfPage(qint32 page);

    /**
     * retry database internal string encoding
     * @retval QByteArray UTF-8 or UTF-16LE or UTF-16BE
     */
    QByteArray getStringCodecName();


private:

    /**
     * cqt odec encoder, deleted by qt
     */
    QTextCodec *codec_;

    /**
     * database file page bytes size
     */
    int pageSize_;

    /**
     * database file handle
     */
    QFile f_;

    /**
     * database file size
     */
    qint64 fileSize_;

    /**
     * database flag string codec
     */
    int textEncoding_;

    /**
     * internal buffer for getPage
     */
    QVector<quint8> pageBuffer_;

    /**
     * follow page 1 and retry the master_table structure
     * @param qint32 page to follow, start always with 1
     * @retval QVector<SqliteTable> array of structures
     */
    QVector<SqliteTable> getMasterTable(quint32 p);

    /**
     * read header of current page
     * @retval SqliteRecovery::PageHeader
     */
    PageHeader readHeader();

    /**
     * retry all records offset of current page
     * @param PageHeader header of current page
     * @retval QVector<quint16> array of current page offset
     */
    QVector<quint16> readOffsets(PageHeader &header);

    /**
     * convert varint to qint64
     * @param quint8*& pointer to buffer
     * @param int& lenght of varint in byte
     * @retval qint64 varint value
     */
    qint64 varint(quint8* &buffer, int &len);

    /**
     * sqlite4 version of 'varint' NOT USED
     */
    qint64 varint4(quint8* &buffer, int &len);

    /**
     * extract records of a LEAF BTREE TABLE offset
     * @param quint16 offset of current page
     * @retval QVector<QVariant> record values to return
     */
    QVector<QVariant> readRecordLeaf(quint16 offset);

    /**
     * get next page of a INTERNAL BTREE TABLE offset
     * @param quint16 offset of current page
     * @retval qint32 next page to follow
     */
    qint32 readRecordInterior(quint16 offset);

    /**
     * create a QVariant starting from a record field
     * @param quint8*& pointer of page buffer, autoincrement to next field
     * @param qint64 type of field to calculate
     * @retval QVariant value of field
     */
    QVariant readDataType(quint8* &ptr, qint64 type);

    /**
     * get current page number, starting from 1
     * @retval int page number
     */
    int getCurrentPage();

    /**
     * fill pagebuffer, reading from file
     * @param int page to read
     */
    void goToPage(int p);

    /**
     * count total database page, count also not used or reserved page
     * @retval int number of total page
     */
    int countPages();

    /**
     * auxiliary method for debug printing view
     * @param PageHeader to print
     */
    void printHeader(const PageHeader &header);

    /**
     * recursive, follow a root page table and return leaf and internal pages
     * @param qint32 root page number of table
     * @param QVector<qint32> * OUTPUT array of INTERNAL BTREE PAGES, can be NULL if you don't want internal leaves
     * @retval QVector<qint32> array of LEAF BTREE PAGES
     */
    QVector<qint32> getPagesOfTable(qint32 page, QVector<qint32> *internal);

    /**
     * convert string type to library enum
     * @param QString type
     */
    TableType stringToType(QString &strType);

};

#endif // SQLITECHUNKRETRIEVER_H
