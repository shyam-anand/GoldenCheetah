/*
 * Copyright (c) 2014 Mark Liversedge (liversedge@gmail.com)
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

#include "RideCache.h"

#include <QtConcurrent>
#include "DBAccess.h"

#include "Context.h"
#include "Athlete.h"
#include "Route.h"
#include "RouteWindow.h"

#include "Zones.h"
#include "HrZones.h"
#include "PaceZones.h"

#include "unistd.h"

RideCache::RideCache(Context *context) : context(context)
{
    progress_ = 100;
    exiting = false;

    // get the new zone configuration fingerprint
    fingerprint = static_cast<unsigned long>(context->athlete->zones()->getFingerprint(context))
                  + static_cast<unsigned long>(context->athlete->paceZones()->getFingerprint())
                  + static_cast<unsigned long>(context->athlete->hrZones()->getFingerprint());

    // set the list
    // populate ride list
    RideItem *last = NULL;
    QStringListIterator i(RideFileFactory::instance().listRideFiles(context->athlete->home->activities()));
    while (i.hasNext()) {
        QString name = i.next();
        QDateTime dt;
        if (RideFile::parseRideFileName(name, &dt)) {
            last = new RideItem(context->athlete->home->activities().canonicalPath(), name, dt, context);
            rides_ << last;
        }
    }

    // load the store - will unstale once cache restored
    load();

    // now refresh just in case.
    refresh();

    // do we have any stale items ?
    connect(context, SIGNAL(configChanged()), this, SLOT(configChanged()));

    // future watching
    connect(&watcher, SIGNAL(finished()), context, SLOT(notifyRefreshEnd()));
    connect(&watcher, SIGNAL(started()), context, SLOT(notifyRefreshStart()));
    connect(&watcher, SIGNAL(progressValueChanged(int)), this, SLOT(progressing(int)));
}

RideCache::~RideCache()
{
    exiting = true;

    // save to store
    save();
}

void
RideCache::configChanged()
{
    // this is the overall config fingerprint, not for a specific
    // ride. We now refresh only when the zones change, and refresh
    // a single ride only when the zones that apply to that ride change
    unsigned long prior = fingerprint;

    // get the new zone configuration fingerprint
    fingerprint = static_cast<unsigned long>(context->athlete->zones()->getFingerprint(context))
                  + static_cast<unsigned long>(context->athlete->paceZones()->getFingerprint())
                  + static_cast<unsigned long>(context->athlete->hrZones()->getFingerprint());

    // if zones etc changed then recalculate metrics
    if (prior != fingerprint) refresh();
}

// add a new ride
void
RideCache::addRide(QString name, bool dosignal)
{
    // ignore malformed names
    QDateTime dt;
    if (!RideFile::parseRideFileName(name, &dt)) return;

    // new ride item
    RideItem *last = new RideItem(context->athlete->home->activities().canonicalPath(), name, dt, context);

    // now add to the list, or replace if already there
    bool added = false;
    for (int index=0; index < rides_.count(); index++) {
        if (rides_[index]->fileName == last->fileName) {
            rides_[index] = last;
            break;
        }
    }

    if (!added) rides_ << last;
    qSort(rides_); // sort by date

    // refresh metrics for *this ride only* 
    // XXX not implemented yet XXX

    if (dosignal) context->notifyRideAdded(last); // here so emitted BEFORE rideSelected is emitted!

#ifdef GC_HAVE_INTERVALS
    //Search routes
    context->athlete->routes->searchRoutesInRide(last->ride());
#endif

    // notify everyone to select it
    context->ride = last;
    context->notifyRideSelected(last);
}

void
RideCache::removeCurrentRide()
{
    if (context->ride == NULL) return;

    RideItem *select = NULL; // ride to select once its gone
    RideItem *todelete = context->ride;

    bool found = false;
    int index=0; // index to wipe out

    // find ours in the list and select the one
    // immediately after it, but if it is the last
    // one on the list select the one before
    for(index=0; index < rides_.count(); index++) {

       if (rides_[index]->fileName == context->ride->fileName) {

          // bingo!
          found = true;
          if (rides_.count()-index > 1) select = rides_[index+1];
          else if (index > 0) select = rides_[index-1];
          break;

       }
    }

    // WTAF!?
    if (!found) {
        qDebug()<<"ERROR: delete not found.";
        return;
    }

    // remove from the cache, before deleting it this is so
    // any aggregating functions no longer see it, when recalculating
    // during aride deleted operation
    rides_.remove(index, 1);

    // delete the file by renaming it
    QString strOldFileName = context->ride->fileName;

    QFile file(context->athlete->home->activities().canonicalPath() + "/" + strOldFileName);
    // purposefully don't remove the old ext so the user wouldn't have to figure out what the old file type was
    QString strNewName = strOldFileName + ".bak";

    // in case there was an existing bak file, delete it
    // ignore errors since it probably isn't there.
    QFile::remove(context->athlete->home->fileBackup().canonicalPath() + "/" + strNewName);

    if (!file.rename(context->athlete->home->fileBackup().canonicalPath() + "/" + strNewName)) {
        QMessageBox::critical(NULL, "Rename Error", tr("Can't rename %1 to %2")
            .arg(strOldFileName).arg(strNewName));
    }

    // remove any other derived/additional files; notes, cpi etc (they can only exist in /cache )
    QStringList extras;
    extras << "notes" << "cpi" << "cpx";
    foreach (QString extension, extras) {

        QString deleteMe = QFileInfo(strOldFileName).baseName() + "." + extension;
        QFile::remove(context->athlete->home->cache().canonicalPath() + "/" + deleteMe);

    }

    // we don't want the whole delete, select next flicker
    context->mainWindow->setUpdatesEnabled(false);

    // select a different ride
    context->ride = select;

    // notify AFTER deleted from DISK..
    context->notifyRideDeleted(todelete);

    // ..but before MEMORY cleared
    todelete->close();
    delete todelete;

    // now we can update
    context->mainWindow->setUpdatesEnabled(true);
    QApplication::processEvents();

    // now select another ride
    context->notifyRideSelected(select);
}

// work with "rideDB.json" XXX not implemented yet
void RideCache::load() {}
void RideCache::save() {}

void
itemRefresh(RideItem *&item)
{
    // need parser to be reentrant !item->refresh();
    if (item->isstale) {
        item->refresh();
usleep(200000);
    }
    item->isstale=false;
}

void
RideCache::progressing(int value)
{
    // we're working away, notfy everyone where we got
    progress_ = 100.0f * (double(value) / double(watcher.progressMaximum()));
    context->notifyRefreshUpdate();
}

// check if we need to refresh the metrics then start the thread if needed
void
RideCache::refresh()
{
    // already on it !
    if (future.isRunning()) return;

    // how many need refreshing ?
    int staleCount = 0;

    foreach(RideItem *item, rides_) {

        // ok set stale so we refresh
        if (item->checkStale()) 
            staleCount++;
    }

    // start if there is work to do
    // and future watcher can notify of updates
    if (staleCount)  {
        future = QtConcurrent::map(rides_, itemRefresh);
        watcher.setFuture(future);
    }
}
