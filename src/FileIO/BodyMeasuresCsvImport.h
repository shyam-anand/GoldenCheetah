/*
 *  * Copyright (c) 2017 Joern Rischmueller (joern.rm@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef GC_BodyMeasuresCsvImport_h
#define GC_BodyMeasuresCsvImport_h

#include "Context.h"
#include "BodyMeasures.h"

#include <QFileDialog>

class BodyMeasuresCsvImport : public QObject {

    Q_OBJECT

    public:

        BodyMeasuresCsvImport(Context *context);
        ~BodyMeasuresCsvImport();

        // get the data
        bool getBodyMeasures(QString &error, QDateTime from, QDateTime to, QList<BodyMeasure> &data);

    signals:
        void downloadStarted(int);
        void downloadProgress(int);
        void downloadEnded(int);

    private:
        Context *context;
        QStringList allowedHeaders;

};
#endif
