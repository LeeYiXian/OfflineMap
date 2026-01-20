#pragma once
#include <QtCore/qglobal.h>

#if defined(MAPGRAPHICSVIEW_LIBRARY)
#  define MAPGRAPHICSVIEW_EXPORT Q_DECL_EXPORT
#else
#  define MAPGRAPHICSVIEW_EXPORT Q_DECL_IMPORT
#endif
