#ifndef SQLITECHUNKRETRIEVER_GLOBAL_H
#define SQLITECHUNKRETRIEVER_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(SQLITECHUNKRETRIEVER_LIBRARY)
#  define SQLITECHUNKRETRIEVERSHARED_EXPORT Q_DECL_EXPORT
#else
#  define SQLITECHUNKRETRIEVERSHARED_EXPORT Q_DECL_IMPORT
#endif

#endif // SQLITECHUNKRETRIEVER_GLOBAL_H
