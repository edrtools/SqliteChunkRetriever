#include "sqlitechunkretriever.h"

SqliteChunkRetriever::SqliteChunkRetriever()
{
    codec_ = nullptr;
}

SqliteChunkRetriever::~SqliteChunkRetriever()
{
    if(f_.isOpen())
        f_.close();
}

int SqliteChunkRetriever::open(QString path)
{
    f_.setFileName(path);
    if (!f_.open(QFile::ReadOnly))
        return -1;

    QByteArray buffer = f_.read(100); // header
    QString sqliteformatstring = buffer.mid(0, 16); // skeep "SQLite format 3\000"

    if (sqliteformatstring.toLower() != "sqlite format 3") {
        f_.close();
        return -1;
    }

    pageSize_ = qFromBigEndian(*(quint16*)(buffer.data() + 16));
    if (pageSize_ == 1)
        pageSize_ = 65536;
    fileSize_ = f_.size();
    textEncoding_ = qFromBigEndian(*(quint32*)(buffer.data() + 56));

    switch (textEncoding_) {
        case 2: codec_ = QTextCodec::codecForName("UTF-16LE"); break;
        case 3: codec_ = QTextCodec::codecForName("UTF-16BE"); break;
        default: codec_ = QTextCodec::codecForName("UTF-8"); break;
    }

    return 0;
}

void SqliteChunkRetriever::close()
{
    f_.close();
}

QVector<SqliteChunkRetriever::SqliteTable> SqliteChunkRetriever::getMasterTable()
{
    QVector<qint32> internal, pages, unallocPages;
    QVector<SqliteTable> tables = getMasterTable(1);

    QVector<qint32> allpages = getPagesOfTable(1, &internal);
    allpages += internal;
    for (SqliteTable &tab: tables) {

        internal = {tab.rootpage};
        if (tab.rootpage) {
            pages = getPagesOfTable(tab.rootpage, &internal);
            tab.child = pages;
            allpages +=  internal + pages;
        }
    }

    for (int i=1; i<=countPages(); i++) {
        if (!allpages.contains(i))
            unallocPages.append(i);
    }

    SqliteTable unallocatedpages;
    unallocatedpages.rootpage = -1;
    unallocatedpages.child = unallocPages;

    tables.append(unallocatedpages);

    return tables;
}

QVector<QByteArray> SqliteChunkRetriever::getUnallocatedOfTable(SqliteTable table)
{
    QVector<QByteArray> ret;
    for (qint32 &p: table.child) {
        QVector<QByteArray> unalloc = getUnallocatedOfPage(p);
        if (unalloc.size()) {
            ret.append(unalloc);
        }
    }
    return ret;
}

QVector<QByteArray> SqliteChunkRetriever::getUnallocatedOfPage(qint32 page)
{
    QVector<QByteArray> ret;
    goToPage(page);
    PageHeader header = readHeader();
    if ((header.flag & 0x1) == 0)
        return ret;

    quint16 start = 8 + (header.num_cells * 2);
    quint16 length = header.cell_offset - start;

    char *ptr = (char*)pageBuffer_.data() + start;
    QByteArray unallocated;
    unallocated.resize(length);
    memcpy(unallocated.data(), ptr, length);

    while (unallocated.size()) {
        if(unallocated.at(0) == 0)
            unallocated.remove(0,1);
        else
            break;
    }

    while (unallocated.size()) {
        if(unallocated.at(unallocated.size()-1) == 0)
            unallocated.remove(unallocated.size()-1,1);
        else
            break;
    }

    if(unallocated.size())
        ret.append(unallocated);

    quint16 freeblock_offset = header.freeblock_offset;

    //if there are freeblocks, pull the data
    while (freeblock_offset != 0) {
        char *ptr = (char*)pageBuffer_.data() + freeblock_offset;
        quint16 next_fb_offset = qFromBigEndian(*(quint16*)ptr);
        ptr +=2;
        quint16 free_block_size = qFromBigEndian(*(quint16*)ptr);
        ptr += 2;

        //QByteArray free_block = f.read(free_block_size);
        QByteArray free_block;
        free_block.resize(free_block_size);
        memcpy(free_block.data(), ptr, free_block_size);

        while (unallocated.size()) {
            if(free_block.at(0) == 0)
                free_block.remove(0,1);
            else
                break;
        }

        while (unallocated.size()) {
            if (free_block.at(free_block.size()-1) == 0)
                free_block.remove(free_block.size() - 1, 1);
            else
                break;
        }

        if (free_block.size())
            ret.append(free_block);

        freeblock_offset = next_fb_offset;
    }

    return ret;
}

QByteArray SqliteChunkRetriever::getStringCodecName()
{
    if (codec_)
        return codec_->name();
    return QByteArray();
}

QVector<SqliteChunkRetriever::SqliteTable> SqliteChunkRetriever::getMasterTable(quint32 p)
{
    goToPage(p);

    QVector<SqliteTable> ret;

    PageHeader header = readHeader();

    if (header.flag == 0x5) {
        // Table B-Tree Interior Cell
        QVector<quint16> offsets = readOffsets(header);

        for(int i=0; i<offsets.length(); i++){

            quint32 page = readRecordInterior(offsets[i]); // using pageBuffer I have to recall goToPage
            ret += getMasterTable(page);
            goToPage(p); // return to current page
        }

        ret += getMasterTable(header.right_most_pointer);
        goToPage(p); // return to current page

    } else if(header.flag == 0xD) {
        // Table B-Tree Leaf Cell
        QVector<quint16> offsets = readOffsets(header);

        for(int i=0; i<offsets.length(); i++){
            goToPage(p);
            QVector<QVariant> tmp = readRecordLeaf(offsets[i]); // using pageBuffer I have to recall goToPage

            SqliteTable strc;

            strc.type = stringToType(tmp[0].toString());
            strc.name = tmp[1].toString();
            strc.tbl_name = tmp[2].toString();
            strc.rootpage = tmp[3].toInt();
            strc.sql = tmp[4].toString();

            ret.append(strc);
        }
    }

    return ret;
}

SqliteChunkRetriever::PageHeader SqliteChunkRetriever::readHeader()
{
    PageHeader header;

    quint8 *ptr = pageBuffer_.data();
    if(getCurrentPage() == 1)
        ptr += 0x64;

    header.flag = *ptr;
    header.freeblock_offset = qFromBigEndian(*(quint16*)(ptr + 1));
    header.num_cells = qFromBigEndian(*(quint16*)(ptr + 3));
    header.cell_offset = qFromBigEndian(*(quint16*)(ptr + 5));
    if(header.cell_offset == 0)
        header.cell_offset = 65536;
    header.num_free_bytes = *(ptr + 7);

    if(header.flag == 0x5)
        header.right_most_pointer = qFromBigEndian(*(quint32*)(ptr + 8));
    else
        header.right_most_pointer = 0;

    return header;
}

QVector<quint16> SqliteChunkRetriever::readOffsets(PageHeader &header)
{
    QVector<quint16> ret;
    quint16 *ptr = (quint16*)(pageBuffer_.data() + 8);
    if(getCurrentPage() == 1)
        ptr += 0x64/2;
    if(header.flag == 0x5)
        ptr += 2;
    for(int i=0; i<header.num_cells; i++)
        ret.append(qFromBigEndian(*(quint16*)(ptr++)));

    return ret;
}

qint64 SqliteChunkRetriever::varint(quint8 *&buffer, int &len)
{
    int byteNum = 0;
    qint64 value = 0;
    bool intComplete = false;
    while (byteNum < 9 && !intComplete) {
        quint8 viByte = *buffer++;
        if ((viByte & 0x80) == 0x80 && byteNum < 8)                 // not yet the last varint byte
            value = (value << 7) | (viByte & 0x7F);                 // add the 7 LSB's to the value
        else if((viByte & 0x80) == 0x80 && byteNum == 8) {          // last stop, let's finalyze the integer
            value = (value << 8) | (viByte);
            intComplete = true;
        } else {                                                    // that's our signal, marks the end of the varint
            value = (value << 7) | (viByte & 0x7F);
            intComplete = true;
        }
        byteNum += 1;
    }
    len = byteNum;
    if(!intComplete){
        qDebug()<<"No valid varint found";
        return -1;
    }

    return value;
}

qint64 SqliteChunkRetriever::varint4(quint8 *&buffer, int &len)
{
    len = 0;
    qint64 u64 = 0;
    quint8 tmp = *buffer;
    if(tmp <=240) {
        len = 1;
        buffer += len;
        return tmp;
    }
    if(tmp <= 248) {
        len = 2;
        buffer += len;
        return (qint64) (240 + (256 * (tmp - 241)) + (*(buffer+1)));
    }
    if(tmp == 249) {
        len = 3;
        buffer += len;
        return (qint64)(2288 + 256 * (*buffer+1) + (*buffer+2));
    }
    if(tmp >= 250) {
        for(int i=(tmp-248); i>=0; i--)
            u64 += (qint64)((qint64)(*(buffer+i)) << (len++*8));
        len++;
        buffer += len;
        return u64;
    }
    return -1;
}

QVector<QVariant> SqliteChunkRetriever::readRecordLeaf(quint16 offset)
{
    QVector<QVariant> ret;
    quint8 *ptr = pageBuffer_.data() + offset;
    int varintlen;

    varint(ptr, varintlen);
    varint(ptr, varintlen);

    quint8 len = *ptr++;
    QVector<qint64> types;
    int bytereaded = 1;
    while (bytereaded < len) {
        qint64 v = varint(ptr, varintlen);
        bytereaded += varintlen;
        types.append(v);
    }

    for (int i=0; i<types.length(); i++) {
        QVariant v = readDataType(ptr, types[i]);
        ret.append(v);
    }

    return ret;
}

qint32 SqliteChunkRetriever::readRecordInterior(quint16 offset)
{
    quint8 *ptr = pageBuffer_.data() + offset;
    int varintlen;
    qint32 pagenumber = qFromBigEndian(*(qint32*)(ptr));
    ptr += 4;
    varint(ptr, varintlen);
    return pagenumber;
}

QVariant SqliteChunkRetriever::readDataType(quint8 *&ptr, qint64 type)
{
    switch (type) {
    case 0: return QVariant();           // NULL
    case 1: return *ptr++;                      // 8-bit twos-complement integer.
    case 2: {
            qint16 v = qFromBigEndian(*(quint16*)(ptr));
            ptr += 2;
            return v;
        }
    case 3: {
            qint32 v = qFromBigEndian(*(quint32*)(ptr-1));
            ptr += 3;
            return v & 0xFFFFFF;
        }
    case 4: {
            qint32 v = qFromBigEndian(*(quint32*)(ptr));
            ptr += 4;
            return v;
        }
    case 5: {
            qint64 v = qFromBigEndian(*(quint64*)(ptr-2));
            ptr += 6;
            return v & 0xFFFFFFFFFFFFL;
        }
    case 6: {
            qint64 v = qFromBigEndian(*(quint64*)(ptr));
            ptr += 8;
            return v;
        }
    case 7: {
            quint64 u64 = qFromBigEndian(*(quint64*)(ptr));
            double v = *(double*)(u64);
            ptr += 8;
            return v;
        }
    case 8: {
            return 0;
        }
    case 9: {
            return 1;
        }
    }

    if (type >= 12) {
        if(type % 2 == 0) {
            int len = (type - 12) / 2;
            QByteArray ba = QByteArray((char*)ptr, len);
            ptr +=len;
            return ba;
        } else {
            int len = (type - 13) / 2;
            QString str = codec_->toUnicode(QByteArray((char*)ptr, len));
            ptr +=len;
            return str;
        }
    }

    return 0;
}

int SqliteChunkRetriever::getCurrentPage()
{
    qint64 index = f_.pos();
    return (index / pageSize_) + 1;
}

void SqliteChunkRetriever::goToPage(int p)
{
    qint64 index = (p-1) * pageSize_;
    f_.seek(index);
    pageBuffer_.resize(pageSize_);
    f_.read((char*)pageBuffer_.data(), pageSize_);
    f_.seek(index);
}

int SqliteChunkRetriever::countPages()
{
    return (fileSize_ / pageSize_);
}

void SqliteChunkRetriever::printHeader(const PageHeader &header)
{
    qDebug()<<"-----------------------------";
    qDebug()<<"flag"<<"0x"+QString::number(header.flag, 16);
    qDebug()<<"freeblock_offset"<<header.freeblock_offset;
    qDebug()<<"num_cells"<<header.num_cells;
    qDebug()<<"cell_offset"<<"0x"+QString::number(header.cell_offset, 16);
    qDebug()<<"num_free_bytes"<<header.num_free_bytes;
    qDebug()<<"right_most_pointer"<<header.right_most_pointer;
    qDebug()<<"-----------------------------";
}

QVector<qint32> SqliteChunkRetriever::getPagesOfTable(qint32 page, QVector<qint32> *internal)
{
    QVector<qint32> ret;
    goToPage(page);
    PageHeader header = readHeader();
    if(header.flag == 0x5) {

        if(internal)
            internal->append(page);

        QVector<quint16> offsets = readOffsets(header);
        QVector<qint32> pages;

        for(quint16 &o: offsets)
            pages.append(readRecordInterior(o));

        for(qint32 &p: pages)
            ret = getPagesOfTable(p, internal) + ret;

        // right most pointer
        ret = getPagesOfTable(header.right_most_pointer, internal) + ret;

    } else if(header.flag == 0xD) {

        ret.append(page);
        return ret;
    }

    return ret;
}

SqliteChunkRetriever::TableType SqliteChunkRetriever::stringToType(QString &strType)
{
    strType = strType.toLower();
    if(strType == "table")
        return TableType::Table;
    if(strType == "index")
        return TableType::Index;
    if(strType == "trigger")
        return TableType::Trigger;
    if(strType == "view")
        return TableType::View;
    return TableType::Unalloc;
}
