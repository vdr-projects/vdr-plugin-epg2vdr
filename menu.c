/*
 * menu.c: EPG2VDR plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "svdrpclient.h"
#include "plgconfig.h"
#include "menu.h"

#include "update.h"

//***************************************************************************
// Object
//***************************************************************************

cMenuDb::cMenuDb()
{
   vdrList = 0;
   vdrCount = 0;
   vdrUuidList = 0;

   timersCacheMaxUpdsp = 0;
   dbInitialized = no;
   connection = 0;

   timerDb = 0;
   vdrDb = 0;
   mapDb = 0;
   timerDoneDb = 0;
   userDb = 0;
   searchtimerDb = 0;
   recordingListDb = 0;
   useeventsDb = 0;

   webLoginEnabled = no;
   user = "@";
   startWithSched = no;

   search = new cSearchTimer();

   userTimes = new cUserTimes;
   initDb();
}

cMenuDb::~cMenuDb()
{
   exitDb();
   delete userTimes;
}

cDbFieldDef startTimeDef("starttime", "starttime", cDBS::ffInt, 0, cDBS::ftData);
cDbFieldDef timerStateDef("state", "state", cDBS::ffAscii, 100, cDBS::ftData);
cDbFieldDef timerActionDef("action", "action", cDBS::ffAscii, 100, cDBS::ftData);
cDbFieldDef groupCountDef("GROUP_COUNT", "count(1)", cDBS::ffUInt, 0, cDBS::ftData);

//***************************************************************************
// initDb / exitDb
//***************************************************************************

int cMenuDb::initDb()
{
   int status = success;

   exitDb();

   connection = new cDbConnection();

   timerDb = new cDbTable(connection, "timers");
   if (timerDb->open() != success) return fail;

   vdrDb = new cDbTable(connection, "vdrs");
   if (vdrDb->open() != success) return fail;

   mapDb = new cDbTable(connection, "channelmap");
   if (mapDb->open() != success) return fail;

   timerDoneDb = new cDbTable(connection, "timersdone");
   if (timerDoneDb->open() != success) return fail;

   userDb = new cDbTable(connection, "users");
   if (userDb->open() != success) return fail;

   searchtimerDb = new cDbTable(connection, "searchtimers");
   if (searchtimerDb->open() != success) return fail;

   recordingListDb = new cDbTable(connection, "recordinglist");
   if (recordingListDb->open() != success) return fail;

   useeventsDb = new cDbTable(connection, "useevents");
   if ((status = useeventsDb->open() != success)) return status;

   if ((status = cParameters::initDb(connection)) != success)
       return status;

   valueStartTime.setField(&startTimeDef);
   timerState.setField(&timerStateDef);
   timerAction.setField(&timerActionDef);

   //-----------
   // select
   //    max(updsp)
   //  from timers

   selectMaxUpdSp = new cDbStatement(timerDb);

   selectMaxUpdSp->build("select ");
   selectMaxUpdSp->bind("UPDSP", cDBS::bndOut, "max(");
   selectMaxUpdSp->build(") from %s", timerDb->TableName());

   status += selectMaxUpdSp->prepare();

   // ----------
   // select
   //    t.*,
   //    t.day + t.starttime div 100 * 60 * 60 + t.starttime % 100 * 60,
   //    v.name, v.state
   //    from timers t, vdrs v
   //    where
   //          t.vdruuid = v.uuid
   //      and (t.state in ('P','R') or t.state is null)
   //    order by t.day, t.starttime

   selectTimers = new cDbStatement(timerDb);

   selectTimers->build("select ");
   selectTimers->setBindPrefix("t.");
   selectTimers->bindAllOut();
   selectTimers->bindTextFree(", t.day + t.starttime div 100 * 60 * 60 + t.starttime % 100 * 60",
                              &valueStartTime, cDBS::bndOut);
   selectTimers->setBindPrefix("v.");
   selectTimers->bind(vdrDb, "NAME", cDBS::bndOut, ", ");
   selectTimers->bind(vdrDb, "UUID", cDBS::bndOut, ", ");
   selectTimers->bind(vdrDb, "STATE", cDBS::bndOut, ", ");
   selectTimers->clrBindPrefix();
   selectTimers->build(" from %s t, %s v where ",
                       timerDb->TableName(), vdrDb->TableName());
   selectTimers->build("t.%s = v.%s",
                       timerDb->getField("VDRUUID")->getDbName(),
                       vdrDb->getField("UUID")->getDbName());

   selectTimers->bindInChar("t", timerDb->getField("STATE")->getDbName(), &timerState, " and (");
   selectTimers->build(" or t.%s is null)", timerDb->getField("STATE")->getDbName());

   selectTimers->bindInChar("t", timerDb->getField("ACTION")->getDbName(), &timerAction, " and (");
   selectTimers->build(" or t.%s is null)", timerDb->getField("ACTION")->getDbName());

   // selectTimers->build(" and (t.%s in ('P','R') or t.%s is null)",
   //                     timerDb->getField("STATE")->getDbName(),
   //                     timerDb->getField("STATE")->getDbName());

   selectTimers->build(" order by t.%s, t.%s",
                       timerDb->getField("DAY")->getDbName(),
                       timerDb->getField("STARTTIME")->getDbName());

   status += selectTimers->prepare();

   // select event by useid
   // select * from eventsview
   //      where useid = ?
   //        and updflg in (.....)

   selectEventById = new cDbStatement(useeventsDb);

   selectEventById->build("select ");
   selectEventById->bindAllOut();
   selectEventById->build(" from %s where ", useeventsDb->TableName());
   selectEventById->bind("USEID", cDBS::bndIn | cDBS::bndSet);
   selectEventById->build(" and %s in (%s)",
                          useeventsDb->getField("UPDFLG")->getDbName(),
                          Us::getNeeded());

   status += selectEventById->prepare();


   // select
   //    t.*,
   //    t.day + t.starttime div 100 * 60 * 60 + t.starttime % 100 * 60,
   //    v.name, v.state
   //    from timers t, vdrs v
   //    where
   //      t.id = ?
   //      and t.vdruuid = v.uuid

   selectTimerById = new cDbStatement(timerDb);

   selectTimerById->build("select ");
   selectTimerById->setBindPrefix("t.");
   selectTimerById->bindAllOut();
   selectTimerById->bindTextFree(", t.day + t.starttime div 100 * 60 * 60 + t.starttime % 100 * 60",
                              &valueStartTime, cDBS::bndOut);
   selectTimerById->setBindPrefix("v.");
   selectTimerById->bind(vdrDb, "NAME", cDBS::bndOut, ", ");
   selectTimerById->bind(vdrDb, "UUID", cDBS::bndOut, ", ");
   selectTimerById->bind(vdrDb, "STATE", cDBS::bndOut, ", ");
   selectTimerById->clrBindPrefix();
   selectTimerById->build(" from %s t, %s v where ", timerDb->TableName(), vdrDb->TableName());
   selectTimerById->setBindPrefix("t.");
   selectTimerById->bind("ID", cDBS::bndIn | cDBS::bndSet);
   selectTimerById->clrBindPrefix();
   selectTimerById->build(" and t.%s = v.%s",
                       timerDb->getField("VDRUUID")->getDbName(),
                       vdrDb->getField("UUID")->getDbName());

   status += selectTimerById->prepare();

   // ----------
   // select ip, svdrp from vdrs
   //   where state = 'attached'

   selectActiveVdrs = new cDbStatement(vdrDb);

   selectActiveVdrs->build("select ");
   selectActiveVdrs->bind("IP", cDBS::bndOut);
   selectActiveVdrs->bind("SVDRP", cDBS::bndOut, ", ");
   selectActiveVdrs->bind("UUID", cDBS::bndOut, ", ");
   selectActiveVdrs->build(" from %s where %s = 'attached' and %s > 0",
                           vdrDb->TableName(),
                           vdrDb->getField("STATE")->getDbName(),
                           vdrDb->getField("SVDRP")->getDbName());

   status += selectActiveVdrs->prepare();

   // ----------
   // select ip, name, state, uuid from vdrs

   selectAllVdrs = new cDbStatement(vdrDb);

   selectAllVdrs->build("select ");
   selectAllVdrs->bind("IP", cDBS::bndOut);
   selectAllVdrs->bind("NAME", cDBS::bndOut, ", ");
   selectAllVdrs->bind("STATE", cDBS::bndOut, ", ");
   selectAllVdrs->bind("UUID", cDBS::bndOut, ", ");
   selectAllVdrs->build(" from %s where svdrp > 0", vdrDb->TableName());

   status += selectAllVdrs->prepare();

   // ----------
   // select * from users

   selectAllUser = new cDbStatement(userDb);

   selectAllUser->build("select ");
   selectAllUser->bindAllOut();
   selectAllUser->build(" from %s", userDb->TableName());

   status += selectAllUser->prepare();

   // ----------
   // select * from searchtimers
   //   where state != 'D'

   selectSearchTimers = new cDbStatement(searchtimerDb);

   selectSearchTimers->build("select ");
   selectSearchTimers->bindAllOut();
   selectSearchTimers->build(" from %s where %s != 'D'",
                             searchtimerDb->TableName(),
                             searchtimerDb->getField("STATE")->getDbName());

   status += selectSearchTimers->prepare();

   // ----------
   // select * from searchtimers
   //   where state != 'D'
   //         and name = ?

   selectSearchTimerByName = new cDbStatement(searchtimerDb);

   selectSearchTimerByName->build("select ");
   selectSearchTimerByName->bindAllOut();
   selectSearchTimerByName->build(" from %s where %s != 'D'",
                                  searchtimerDb->TableName(),
                                  searchtimerDb->getField("STATE")->getDbName());
   selectSearchTimerByName->bind("NAME", cDBS::bndIn | cDBS::bndSet, " and ");

   status += selectSearchTimerByName->prepare();

   // ----------
   // select updsp, id, state, title, shorttext,
   //    from timersdone where state like ? order by title

   selectDoneTimerByStateTitleOrder = new cDbStatement(timerDoneDb);

   selectDoneTimerByStateTitleOrder->build("select ");
   selectDoneTimerByStateTitleOrder->bind("ID", cDBS::bndOut);
   selectDoneTimerByStateTitleOrder->bind("STATE", cDBS::bndOut, ", ");
   selectDoneTimerByStateTitleOrder->bind("STARTTIME", cDBS::bndOut, ", ");
   selectDoneTimerByStateTitleOrder->bind("UPDSP", cDBS::bndOut, ", ");
   selectDoneTimerByStateTitleOrder->bind("TITLE", cDBS::bndOut, ", ");
   selectDoneTimerByStateTitleOrder->bind("SHORTTEXT", cDBS::bndOut, ", ");
   selectDoneTimerByStateTitleOrder->clrBindPrefix();
   selectDoneTimerByStateTitleOrder->build(" from %s where ", timerDoneDb->TableName());
   selectDoneTimerByStateTitleOrder->bindCmp(0, "STATE", 0, "like");
   selectDoneTimerByStateTitleOrder->build(" order by %s", timerDoneDb->getField("TITLE")->getDbName());

   status += selectDoneTimerByStateTitleOrder->prepare();

   // same statement with
   //  'order by starttimer'

   selectDoneTimerByStateTimeOrder  = new cDbStatement(timerDoneDb);
   selectDoneTimerByStateTimeOrder->build("select ");
   selectDoneTimerByStateTimeOrder->bind("ID", cDBS::bndOut);
   selectDoneTimerByStateTimeOrder->bind("STATE", cDBS::bndOut, ", ");
   selectDoneTimerByStateTimeOrder->bind("STARTTIME", cDBS::bndOut, ", ");
   selectDoneTimerByStateTimeOrder->bind("UPDSP", cDBS::bndOut, ", ");
   selectDoneTimerByStateTimeOrder->bind("TITLE", cDBS::bndOut, ", ");
   selectDoneTimerByStateTimeOrder->bind("SHORTTEXT", cDBS::bndOut, ", ");
   selectDoneTimerByStateTimeOrder->clrBindPrefix();
   selectDoneTimerByStateTimeOrder->build(" from %s where ", timerDoneDb->TableName());
   selectDoneTimerByStateTimeOrder->bindCmp(0, "STATE", 0, "like");
   selectDoneTimerByStateTimeOrder->build(" order by %s", timerDoneDb->getField("STARTTIME")->getDbName());

   status += selectDoneTimerByStateTimeOrder->prepare();

   // select *
   //   from recordinglist where
   //      (state <> 'D' or state is null)
   //      order by starttime

   selectRecordings = new cDbStatement(recordingListDb);

   selectRecordings->build("select ");
   selectRecordings->bindAllOut();
   selectRecordings->build(" from %s where ", recordingListDb->TableName());
   selectRecordings->build(" (%s <> 'D' or %s is null)",
                                  recordingListDb->getField("STATE")->getDbName(),
                                  recordingListDb->getField("STATE")->getDbName());
   selectRecordings->build(" order by %s", recordingListDb->getField("STARTTIME")->getDbName());

   status += selectRecordings->prepare();

   // select category, genre, count(1)
   //   from recordinglist
   //     where
   //      (state <> 'D' or state is null)
   //     group by ifnull(category, 'unknown'), ifnull(genre, 'unknown')
   //     order by ifnull(category, 'unknown'), ifnull(genre, 'unknown')

   groupCount.setField(&groupCountDef);

   selectRecordingsGrouped = new cDbStatement(recordingListDb);

   selectRecordingsGrouped->build("select ");
   selectRecordingsGrouped->bind("CATEGORY", cDBS::bndOut);
   selectRecordingsGrouped->bind("GENRE", cDBS::bndOut, ", ");
   selectRecordingsGrouped->bind(&groupCount, cDBS::bndOut, ", ");
   selectRecordingsGrouped->build(" from %s where ", recordingListDb->TableName());
   selectRecordingsGrouped->build(" (%s <> 'D' or %s is null)",
                                  recordingListDb->getField("STATE")->getDbName(),
                                  recordingListDb->getField("STATE")->getDbName());
   selectRecordingsGrouped->build(" group by ifnull(%s, 'unknown'), ifnull(%s, 'unknown')",
                                  recordingListDb->getField("CATEGORY")->getDbName(),
                                  recordingListDb->getField("GENRE")->getDbName());
   selectRecordingsGrouped->build(" order by ifnull(%s, 'unknown'), ifnull(%s, 'unknown')",
                                  recordingListDb->getField("CATEGORY")->getDbName(),
                                  recordingListDb->getField("GENRE")->getDbName());

   status += selectRecordingsGrouped->prepare();

   // select *
   //   from recordinglist where
   //      (state <> 'D' or state is null)
   //      and title like ?
   //      and shorttext like ?

   selectRecordingForEvent = new cDbStatement(recordingListDb);

   selectRecordingForEvent->build("select ");
   selectRecordingForEvent->bindAllOut();
   selectRecordingForEvent->build(" from %s where ", recordingListDb->TableName());
   selectRecordingForEvent->build(" (%s <> 'D' or %s is null)",
                                  recordingListDb->getField("STATE")->getDbName(),
                                  recordingListDb->getField("STATE")->getDbName());
   selectRecordingForEvent->bindCmp(0, "TITLE", 0, "like", " and ");
   selectRecordingForEvent->bindCmp(0, "SHORTTEXT", 0, "like", " and ");

   status += selectRecordingForEvent->prepare();

   // select *
   //   from recordinglist where
   //      (state <> 'D' or state is null)
   //   and epglvr(title, ?) < 50
//   // order by lv

   selectRecordingForEventByLv = new cDbStatement(recordingListDb);

   selectRecordingForEventByLv->build("select ");
   selectRecordingForEventByLv->bindAllOut();
   // selectRecordingForEventByLv->bindTextFree(", epglvr(title, ?)", recordingListDb->getValue("TITLE"), cDBS::bndIn); -> core :o
   selectRecordingForEventByLv->build(" from %s where ", recordingListDb->TableName());
   selectRecordingForEventByLv->build(" (%s <> 'D' or %s is null)",
                                  recordingListDb->getField("STATE")->getDbName(),
                                  recordingListDb->getField("STATE")->getDbName());
   selectRecordingForEventByLv->bindTextFree("and epglvr(title, ?) < 50", recordingListDb->getValue("TITLE"), cDBS::bndIn);

   status += selectRecordingForEventByLv->prepare();

   // ----------
   // select channelname
   //   from channelmap
   //   where channelid = ?

   selectChannelFromMap = new cDbStatement(mapDb);

   selectChannelFromMap->build("select ");
   selectChannelFromMap->bind("CHANNELNAME", cDBS::bndOut);
   selectChannelFromMap->build(" from %s where ", mapDb->TableName());
   selectChannelFromMap->bind("CHANNELID", cDBS::bndIn | cDBS::bndSet);

   status += selectChannelFromMap->prepare();

   // search timer stuff

   if (!status)
   {
      if ((status = search->initDb()) != success)
         return status;
   }

   // ---------------------------------
   // prepare vdr-list for EditStraItem

   int i = 0;

   vdrDb->countWhere("svdrp > 0", vdrCount);
   vdrList = new char*[vdrCount];
   vdrUuidList = new char*[vdrCount];;

   vdrDb->clear();

   for (int f = selectAllVdrs->find(); f; f = selectAllVdrs->fetch())
   {
      asprintf(&vdrList[i], "%s%c", vdrDb->getStrValue("NAME"), vdrDb->hasValue("STATE", "attached") ? '*' : ' ');
      vdrUuidList[i] = strdup(vdrDb->getStrValue("UUID"));
      i++;
   }

   selectAllVdrs->freeResult();

   // some parameters

   user = "@";
   getParameter("webif", "needLogin", webLoginEnabled);

   if (webLoginEnabled)
      user += Epg2VdrConfig.user;

   getParameter(user.c_str(), "startWithSched", startWithSched);

   // prepare User Times (quickTimes)

   initUserTimes();

   // ------

   dbInitialized = yes;

   return status;
}

int cMenuDb::exitDb()
{
   dbInitialized = no;

   search->exitDb();

   if (connection)
   {
      cParameters::exitDb();

      delete timerDb;                  timerDb = 0;
      delete selectEventById;          selectEventById = 0;
      delete vdrDb;                    vdrDb = 0;
      delete mapDb;                    mapDb = 0;
      delete timerDoneDb;              timerDoneDb = 0;
      delete userDb;                   userDb = 0;
      delete searchtimerDb;            searchtimerDb = 0;
      delete recordingListDb;          recordingListDb = 0;
      delete useeventsDb;              useeventsDb = 0;

      delete selectTimers;             selectTimers = 0;
      delete selectMaxUpdSp;           selectMaxUpdSp = 0;
      delete selectTimerById;          selectTimerById = 0;
      delete selectActiveVdrs;         selectActiveVdrs = 0;
      delete selectAllVdrs;            selectAllVdrs = 0;
      delete selectAllUser;            selectAllUser = 0;
      delete selectSearchTimers;       selectSearchTimers = 0;
      delete selectSearchTimerByName;  selectSearchTimerByName = 0;

      delete selectDoneTimerByStateTitleOrder; selectDoneTimerByStateTitleOrder = 0;
      delete selectDoneTimerByStateTimeOrder;  selectDoneTimerByStateTimeOrder = 0;
      delete selectRecordingForEvent;          selectRecordingForEvent = 0;
      delete selectRecordingForEventByLv;      selectRecordingForEventByLv = 0;
      delete selectRecordings;                 selectRecordings = 0;
      delete selectRecordingsGrouped;          selectRecordingsGrouped = 0;
      delete selectChannelFromMap;             selectChannelFromMap = 0;

      delete connection;           connection = 0;

      for (int i = 0; i < vdrCount; i++)
      {
         free(vdrList[i]);
         free(vdrUuidList[i]);
      }

      delete vdrList;
      delete vdrUuidList;
   }

   return done;
}

//***************************************************************************
// Init Users List
//***************************************************************************

int cMenuDb::initUserList(char**& userList, int& count, long int& loginEnabled)
{
   int i = 0;
   count = 0;

   if (!dbConnected())
      return fail;

   getParameter("webif", "needLogin", loginEnabled);

   userDb->countWhere("1 = 1", count);
   userList = new char*[count];
   memset(userList, 0, count*sizeof(char*));

   userDb->clear();

   for (int f = selectAllUser->find(); f; f = selectAllUser->fetch())
   {
      if (userDb->getIntValue("ACTIVE") > 0)
         asprintf(&userList[i++], "%s", userDb->getStrValue("USER"));
   }

   return success;
}

//***************************************************************************
// Init Times
//***************************************************************************

int cMenuDb::initUserTimes()
{
   char* p;
   char times[500+TB] = "";

   userTimes->clear();
   getParameter(user.c_str(), "quickTimes", times);

   tell(3, "DEBUG: got quickTimes [%s] for user '%s'", times, user.c_str());

   // parse string like:
   //    PrimeTime=20:15~EarlyNight=22:20~MidNight=00:00~LateNight=02:00

   p = strtok(times, "~");

   while (p)
   {
      char* time;

      if ((time = strchr(p, '=')))
         *time++ = 0;

      if (!isEmpty(time))
         userTimes->add(time, p);
      else
         tell(0, "Error: Ignoring invalid quickTime '%s'", p);

      p = strtok(0, "~");
   }

   return done;
}

//***************************************************************************
// Lookup Timer
//***************************************************************************

int cMenuDb::lookupTimer(const cEvent* event, int& timerMatch, int& remote, char& type, int force)
{
   int maxSp = 0;

   timerMatch = tmNone;
   remote = no;

   if (!event)
      return 0;

   // on change create lookup cache

   timerDb->clear();
   selectMaxUpdSp->execute();
   maxSp = timerDb->getIntValue("UPDSP");
   selectMaxUpdSp->freeResult();

   if (maxSp != timersCacheMaxUpdsp)
   {
      tell(1, "DEBUG: Update timer lookup cache %s", force ? "(forced)": "");

      timersCacheMaxUpdsp = maxSp;
      timers.Clear();
      timerDb->clear();
      timerState.setValue("P,R,u");
      timerAction.setValue("C,M,A,F,a");

      for (int f = selectTimers->find(); f && dbConnected(); f = selectTimers->fetch())
      {
         cTimerInfo* info = new cTimerInfo;

         info->timerid = timerDb->getValue("ID")->getIntValue();
         info->state = timerDb->getValue("STATE")->getCharValue();
         info->type = timerDb->getValue("TYPE")->getCharValue();
         info->starttime = valueStartTime.getIntValue();
         info->eventid = timerDb->getIntValue("EVENTID");
         strcpy(info->channelid, timerDb->getStrValue("CHANNELID"));
         strcpy(info->uuid, timerDb->getStrValue("VDRUUID"));

         timers.Add(info);
      }

      selectTimers->freeResult();
   }

   // lookup timer in cache

   for (const cTimerInfo* ifo = timers.First(); ifo; ifo = timers.Next(ifo))
   {
      if (ifo->eventid == event->EventID())
      {
         remote = strcmp(ifo->uuid, Epg2VdrConfig.uuid) != 0;
         timerMatch = tmFull;
         type = ifo->type;
         return ifo->timerid;
      }

      else if (strcmp(ifo->channelid, event->ChannelID().ToString()) == 0
               && ifo->starttime == event->StartTime())  // #TODO? also around this time
      {
         remote = strcmp(ifo->uuid, Epg2VdrConfig.uuid) != 0;
         timerMatch = tmFull;
         type = ifo->type;
         return ifo->timerid;
      }
   }

   return 0;
}

//***************************************************************************
// Modify Timer
//
//    - timerRow contain the actual vdrUuid
//    - for move destUuid or newType is set
//***************************************************************************

int cMenuDb::modifyTimer(cDbRow* timerRow, const char* destUuid, char destType)
{
   int timerid = timerDb->getIntValue("ID");
   int knownTimer = !timerRow->hasValue("ID", (long)na);
   int move = no;

   if (knownTimer)
   {
      // mive to other VDR?

      if (!timerRow->hasValue("VDRUUID", destUuid))
         move = yes;

      // move to other type

      else if (!timerRow->hasCharValue("TYPE", destType))
         move = yes;
   }

   // lookup known (existing) timer

   connection->startTransaction();

   if (knownTimer)
   {
      timerDb->clear();
      timerDb->getRow()->copyValues(timerRow, cDBS::ftPrimary);

      if (!timerDb->find())
      {
         connection->commit();

         tell(0, "Timer (%d) at vdr '%s' not found, aborting modify request!",
              timerid, timerDb->getStrValue("VDRUUID"));

         return fail;
      }

      // found and all values are loaded!
   }

   if (move)
   {
      // request 'D'elete of 'old' timer

      timerDb->setCharValue("ACTION", taDelete);
      timerDb->setValue("SOURCE", Epg2VdrConfig.uuid);
      timerDb->update();

      // create new on other vdr

      timerDb->getRow()->copyValues(timerRow, cDBS::ftData);     // takeover all data (can be modified by user)
      timerDb->setValue("ID", 0);                      // don't care on insert!
      timerDb->setValue("VDRUUID", destUuid);
      timerDb->setCharValue("ACTION", taCreate);
      timerDb->setValue("SOURCE", Epg2VdrConfig.uuid);
      timerDb->setCharValue("TYPE", destType);
      timerDb->insert();

      tell(0, "Created 'move' request for timer (%d) at vdr '%s' and type '%s'",
           timerid, timerDb->getStrValue("VDRUUID"), timerDb->getStrValue("TYPE"));
   }
   else
   {
      // create 'C'reate oder 'M'odify request ...

      timerDb->getRow()->copyValues(timerRow, cDBS::ftData);

      timerDb->setCharValue("ACTION", knownTimer ? taModify : taCreate);
      timerDb->getValue("STATE")->setNull();
      timerDb->setValue("SOURCE", Epg2VdrConfig.uuid);
      timerDb->setCharValue("TYPE", destType);

      if (!knownTimer)
         timerDb->setValue("NAMINGMODE", tnmAuto);

      if (knownTimer)
         timerDb->update();
      else
         timerDb->insert();

      tell(0, "Created '%s' request for timer (%d) at vdr '%s'",
           knownTimer ? "modify" : "create",
           timerid, timerDb->getStrValue("VDRUUID"));
   }

   connection->commit();
   triggerVdrs("TIMERJOB");  // trigger all!

   return success;
}

//***************************************************************************
// Create Switch Timer
//***************************************************************************

int cMenuDb::createSwitchTimer(const cEvent* event)
{
   int timerMatch = tmNone;
   int remote = no;
   char type;

   // already or nearly started?

   if (!event || event->StartTime() <= time(0)+tmeSecondsPerMinute)
      return done;

   // already created?

   if (lookupTimer(event, timerMatch, remote, type))
   {
      if (!remote && type == ttView)
         return done;
   }

   cDbRow* timerRow = newTimerRowFromEvent(event);

   timerRow->setValue("STARTTIME", l2hhmm(timerRow->getIntValue("_STARTTIME")));
   createTimer(timerRow, Epg2VdrConfig.uuid, ttView);

   tell(0, "Switch timer for '%s' at '%s' created",
        event->Title(), l2pTime(event->StartTime()).c_str());

   return done;
}

//***************************************************************************
// Create Timer
//***************************************************************************

int cMenuDb::createTimer(cDbRow* timerRow, const char* destUuid, int type)
{
   long int manualTimer2Done;

   getParameter("epgd", "manualTimer2Done", manualTimer2Done);

   // Timer 'C'reate request ...

   timerDb->clear();
   timerDb->getRow()->copyValues(timerRow, cDBS::ftData);

   timerDb->setValue("VDRUUID", destUuid);
   timerDb->setCharValue("TYPE", type);
   // timerDb->setCharValue("ACTIVE", type != ttView);  // #TODO will this better?
   timerDb->setCharValue("ACTION", taCreate);
   timerDb->setValue("SOURCE", Epg2VdrConfig.uuid);
   timerDb->setValue("NAMINGMODE", tnmAuto);

   if (manualTimer2Done && type != ttView)
   {
      useeventsDb->clear();
      useeventsDb->setValue("USEID", timerRow->getIntValue("EVENTID"));

      if (selectEventById->find())
      {
         timerDoneDb->clear();
         timerDoneDb->setCharValue("STATE", tdsTimerRequested);
         timerDoneDb->setValue("SOURCE", Epg2VdrConfig.uuid);

         timerDoneDb->setValue("CHANNELID", useeventsDb->getStrValue("CHANNELID"));
         timerDoneDb->setValue("STARTTIME", useeventsDb->getIntValue("STARTTIME"));
         timerDoneDb->setValue("DURATION", useeventsDb->getIntValue("DURATION"));
         timerDoneDb->setValue("TITLE", useeventsDb->getStrValue("TITLE"));
         timerDoneDb->setValue("COMPTITLE", useeventsDb->getStrValue("COMPTITLE"));

         if (!useeventsDb->getValue("SHORTTEXT")->isEmpty())
            timerDoneDb->setValue("SHORTTEXT", useeventsDb->getStrValue("SHORTTEXT"));
         if (!useeventsDb->getValue("COMPSHORTTEXT")->isEmpty())
            timerDoneDb->setValue("COMPSHORTTEXT", useeventsDb->getStrValue("COMPSHORTTEXT"));

         if (!useeventsDb->getValue("LONGDESCRIPTION")->isEmpty())
            timerDoneDb->setValue("LONGDESCRIPTION", useeventsDb->getStrValue("LONGDESCRIPTION"));
         if (!useeventsDb->getValue("COMPLONGDESCRIPTION")->isEmpty())
            timerDoneDb->setValue("COMPLONGDESCRIPTION", useeventsDb->getStrValue("COMPLONGDESCRIPTION"));

         if (!useeventsDb->getValue("EPISODECOMPNAME")->isEmpty())
            timerDoneDb->setValue("EPISODECOMPNAME", useeventsDb->getStrValue("EPISODECOMPNAME"));
         if (!useeventsDb->getValue("EPISODECOMPSHORTNAME")->isEmpty())
            timerDoneDb->setValue("EPISODECOMPSHORTNAME", useeventsDb->getStrValue("EPISODECOMPSHORTNAME"));

         if (!useeventsDb->getValue("EPISODECOMPPARTNAME")->isEmpty())
            timerDoneDb->setValue("EPISODECOMPPARTNAME", useeventsDb->getStrValue("EPISODECOMPPARTNAME"));
         if (!useeventsDb->getValue("EPISODELANG")->isEmpty())
            timerDoneDb->setValue("EPISODELANG", useeventsDb->getStrValue("EPISODELANG"));
         if (!useeventsDb->getValue("EPISODESEASON")->isEmpty())
            timerDoneDb->setValue("EPISODESEASON", useeventsDb->getIntValue("EPISODESEASON"));
         if (!useeventsDb->getValue("EPISODEPART")->isEmpty())
            timerDoneDb->setValue("EPISODEPART", useeventsDb->getIntValue("EPISODEPART"));

         // lookup channelname

         const char* channelname = "";

         mapDb->clear();
         mapDb->setValue("CHANNELID", useeventsDb->getStrValue("CHANNELID"));

         if (selectChannelFromMap->find())
         {
            channelname = mapDb->getStrValue("CHANNELNAME");

            if (isEmpty(channelname))
               channelname = useeventsDb->getStrValue("CHANNELID");

            timerDoneDb->setValue("CHANNELNAME", channelname);
         }

         selectChannelFromMap->freeResult();
      }

      selectEventById->freeResult();

      timerDoneDb->insert();
      timerDb->setValue("DONEID", timerDoneDb->getLastInsertId());
   }

   timerDb->insert();

   triggerVdrs("TIMERJOB", destUuid);

   tell(0, "Created 'create' request for event '%ld' at vdr '%s'",
        timerDb->getIntValue("EVENTID"), timerDb->getStrValue("VDRUUID"));

   return done;
}

//***************************************************************************
// Delete Timer
//***************************************************************************

int cMenuDb::deleteTimer(long timerid)
{
   timerDb->clear();
   timerDb->setValue("ID", timerid);

   if (!selectTimerById->find())
   {
      selectTimerById->freeResult();
      tell(0, "Timer (%ld) not found, maybe deleted from other user, aborting", timerid);
      Skins.Message(mtInfo, tr("Timer not found, maybe deleted from other user"));

      return fail;
   }

   selectTimerById->freeResult();

   timerDb->setCharValue("ACTION", taDelete);
   timerDb->setValue("SOURCE", Epg2VdrConfig.uuid);
   timerDb->update();

   tell(0, "Created delete request for timer (%ld)", timerid);

   triggerVdrs("TIMERJOB", timerDb->getStrValue("VDRUUID"));

   return success;
}

//***************************************************************************
// Trigger VDRs
//***************************************************************************

int cMenuDb::triggerVdrs(const char* trg, const char* uuid, const char* options)
{
   int count = 0;

   if (!dbConnected())
      return 0;

   if (!isEmpty(uuid))
   {
      if (triggerVdr(uuid, trg, options) == success)
         count++;

      return count;
   }

   vdrDb->clear();

   for (int f = selectActiveVdrs->find(); f; f = selectActiveVdrs->fetch())
   {
      char* uuid = strdup(vdrDb->getStrValue("UUID"));
      tell(1, "Trigger VDR '%s' with '%s'", uuid, trg);

      if (triggerVdr(uuid, trg, options) == success)
         count++;

      free(uuid);
   }

   selectActiveVdrs->freeResult();

   return count;
}

int cMenuDb::triggerVdr(const char* uuid, const char* trg, const char* options)
{
   const char* plgs[] = { "epg2vdr", 0 }; // "scraper2vdr"

   // myself - SVDR to myself will lock vdr!!

   if (strcmp(uuid, Epg2VdrConfig.uuid) == 0)
   {
      if (strcasecmp(trg, "TIMERJOB") == 0)
      {
         tell(2, "Trigger myself to update timer-jobs");
         oUpdate->triggerTimerJobs();
      }

      return success;
   }

   if (!dbConnected())
   {
      tell(0, "Error: Lost database connection, aborting trigger");
      return fail;
   }

   vdrDb->clear();
   vdrDb->setValue("UUID", uuid);

   if (!vdrDb->find())
   {
      tell(0, "Error: Can't lookup VDR with uuid '%s', skipping trigger", uuid);
      return fail;
   }

   const char* ip = vdrDb->getStrValue("IP");
   unsigned int port = vdrDb->getIntValue("SVDRP");

   vdrDb->reset();

   // svdrp connection

   cSvdrpClient cl(ip, port);

   // open tcp connection

   if (cl.open() == success)
   {
      for (int i = 0; plgs[i]; i++)
      {
         char* command = 0;
         cList<cLine> result;

         asprintf(&command, "PLUG %s %s %s", plgs[i], trg, !isEmpty(options) ? options : "");

         tell(0, "Send '%s' to '%s' at '%s:%d'", command, plgs[i], ip, port);

         if (!cl.send(command))
            tell(0, "Error: Send '%s' to '%s' at '%s:%d' failed!",
                 command, plgs[i], ip, port);
         else
            cl.receive(&result, 2);

         free(command);
      }

      cl.close(no);
   }

   return success;
}
