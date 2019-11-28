/*
 * menurec.c: EPG2VDR plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

/*
 Inbetribnahme:
    truncate recordinglist; commit;
    #> systemctrl start vdr
    #> svdrpsend PLUG epg2vdr UPDREC
*/

#include <vdr/menuitems.h>
#include <vdr/status.h>
#include <vdr/menu.h>
#include <vdr/videodir.h>
#include <vdr/interface.h>

#include "lib/xml.h"

#include "plgconfig.h"
#include "menu.h"
#include "ttools.h"

//***************************************************************************
// Item Base Class
//***************************************************************************

class cMenuDbRecordingItemBase : public cOsdItem
{
   public:

      cMenuDbRecordingItemBase() {}
      virtual ~cMenuDbRecordingItemBase() { free(name); }

      virtual const char* Name()            const { return name; }
      virtual const cRecording* Recording() const = 0;
      virtual bool isDirectory()            const = 0;

   protected:

      char* name {nullptr};
};

//***************************************************************************
// Class Recording Item
//***************************************************************************

class cMenuDbRecordingItem : public cMenuDbRecordingItemBase
{
   public:

      cMenuDbRecordingItem(cMenuDb* db, const cRecording* Recording);
      virtual ~cMenuDbRecordingItem();


      const cRecording* Recording() const override { return recording; }
      bool isDirectory()            const override { return false; }
      void SetRecording(const cRecording* Recording) { recording = Recording; }
      virtual void SetMenuItem(cSkinDisplayMenu* DisplayMenu, int Index, bool Current, bool Selectable);

   private:

      const cRecording* recording {nullptr};
      cMenuDb* menuDb {nullptr};
};

cMenuDbRecordingItem::cMenuDbRecordingItem(cMenuDb* db, const cRecording* Recording)
{
   menuDb = db;
   recording = Recording;
   name = nullptr;

   uint level {0};

   for (const char* p = Recording->Title(); *p; p++)
      if (*p == '~')
         level++;

   SetText(Recording->Title('\t', true, level));
   tell(0, "Added recording for file '%s'", Recording->Title());
}

cMenuDbRecordingItem::~cMenuDbRecordingItem()
{
}

void cMenuDbRecordingItem::SetMenuItem(cSkinDisplayMenu* DisplayMenu, int Index, bool Current, bool Selectable)
{
   if (!DisplayMenu->SetItemRecording(recording, Index, Current, Selectable, 0/*level*/, 0, 0))
      DisplayMenu->SetItem(Text(), Index, Current, Selectable);
}

//***************************************************************************
// Class Folder Item
//***************************************************************************

class cMenuDbRecordingFolderItem : public cMenuDbRecordingItemBase
{
   public:

      cMenuDbRecordingFolderItem(cMenuDb* db, const char* title, int count);
      virtual ~cMenuDbRecordingFolderItem();

      // void IncrementCounter(bool New);

      const cRecording* Recording() const override { return tmpRecording; }
      bool isDirectory()            const override { return true; }
      virtual void SetMenuItem(cSkinDisplayMenu* DisplayMenu, int Index, bool Current, bool Selectable);

   private:

      cRecording* tmpRecording {nullptr};
      uint totalEntries {0};
      uint newEntries {0};
      cMenuDb* menuDb {nullptr};
};

cMenuDbRecordingFolderItem::cMenuDbRecordingFolderItem(cMenuDb* db, const char* title, int count)
{
   menuDb = db;
   name = strdup(title);
   totalEntries = count;
   newEntries = 0;

   if (tmpRecording)
   {
      delete tmpRecording;
      tmpRecording = nullptr;
   }

   char* dummy;
   asprintf(&dummy, "\t%s/%s", name, "2000-01-01.00.00.0-0.rec");

   tmpRecording = new cRecording(dummy);
   SetText(cString::sprintf("%d\t\t%d\t%s", totalEntries, newEntries, name));

   free(dummy);
}

cMenuDbRecordingFolderItem::~cMenuDbRecordingFolderItem()
{
   delete tmpRecording;
}

void cMenuDbRecordingFolderItem::SetMenuItem(cSkinDisplayMenu* DisplayMenu, int Index, bool Current, bool Selectable)
{
  if (!DisplayMenu->SetItemRecording(tmpRecording, Index, Current, Selectable, 0, totalEntries, newEntries))
     DisplayMenu->SetItem(Text(), Index, Current, Selectable);
}

//***************************************************************************
// Class cMenuDbRecordings
//***************************************************************************
//***************************************************************************
// Object
//***************************************************************************

cMenuDbRecordings::cMenuDbRecordings(const char* Base, int Level, const char* Group, bool OpenSubMenus)
   : cOsdMenu(Base ? Base : tr("Recordings"), 9, 6, 6)
{
   menuDb = new cMenuDb;
   level = Level;

   SetMenuCategory(mcRecording);
   group = Group ? strdup(Group) : nullptr;
   base = Base ? strdup(Base) : nullptr;

   Display(); // this keeps the higher level menus from showing up briefly when pressing 'Back' during replay

   tell(0, "open recording menu '%s' for group '%s'", base, group ? group : "");

   if (menuDb->dbConnected())
   {
      if (!isEmpty(group))
         LoadGroup(group);
      else
         LoadGrouped();

      // LoadPlain();
   }

   if (Current() < 0)
      SetCurrent(First());

   // else if (OpenSubMenus && (cReplayControl::LastReplayed()))
   // {
   //    if (!*path || Level < strcountchr(path, FOLDERDELIMCHAR))
   //    {
   //       if (Open(true))
   //          return;
   //    }
   // }

   Display();
   SetHelpKeys();
}

cMenuDbRecordings::~cMenuDbRecordings()
{
   // cMenuDbRecordingItem* ri = (cMenuDbRecordingItem*)Get(Current());

   delete menuDb;

   free(group);
   free(base);
}

//***************************************************************************
// Set Help Keys
//***************************************************************************

void cMenuDbRecordings::SetHelpKeys(void)
{
  cMenuDbRecordingItemBase* ri = (cMenuDbRecordingItemBase*)Get(Current());
  int NewHelpKeys = 0;

  if (ri)
  {
     if (ri->isDirectory())
        NewHelpKeys = 1;
     else
        NewHelpKeys = 2;
  }

  if (NewHelpKeys != helpKeys)
  {
     switch (NewHelpKeys)
     {
        case 0: SetHelp(NULL); break;
        case 1: SetHelp(tr("Button$Open"), NULL, NULL, tr("Button$Edit")); break;
        case 2: SetHelp(RecordingCommands.Count() ? tr("Commands") : tr("Button$Play"), tr("Button$Rewind"), tr("Button$Delete"), tr("Button$Info"));
        default: ;
     }

     helpKeys = NewHelpKeys;
  }
}

//***************************************************************************
// Load Grouped
//***************************************************************************

void cMenuDbRecordings::LoadGrouped(bool Refresh)
{
   Clear();

   menuDb->recordingListDb->clear();

   for (int res = menuDb->selectRecordingsGrouped->find(); res; res = menuDb->selectRecordingsGrouped->fetch())
   {
      const char* group = menuDb->recordingListDb->getStrValue("GROUP");
      tell(0, "Added recording folder '%s'", group);
      Add(new cMenuDbRecordingFolderItem(menuDb, group, menuDb->groupCount.getIntValue()));
   }

   menuDb->selectRecordingsGrouped->freeResult();

   if (Refresh)
      Display();

   return ;
}

//***************************************************************************
// Load recordings of a Group
//***************************************************************************

void cMenuDbRecordings::LoadGroup(const char* group, bool Refresh)
{
   if (!cRecordings::GetRecordingsRead(recordingsStateKey))
      return ;

   recordingsStateKey.Remove();
   cRecordings* Recordings = cRecordings::GetRecordingsWrite(recordingsStateKey);

   if (!Recordings)
   {
      tell(0, "Fatal: Can't get recording lock");
      return ;
   }

   Clear();

   menuDb->recordingListDb->clear();
   menuDb->recordingListDb->setValue("GROUP", group);

   for (int res = menuDb->selectRecordingByGroup->find(); res; res = menuDb->selectRecordingByGroup->fetch())
   {
      char* fileName {nullptr};
      asprintf(&fileName, "%s/%s", cVideoDirectory::Name(), menuDb->recordingListDb->getStrValue("PATH"));
      const cRecording* recording = Recordings->GetByName(fileName);

      if (recording)
         Add(new cMenuDbRecordingItem(menuDb, recording));
      else
         tell(0, "Error: Recording for file '%s' not found, skipped", fileName);

      free(fileName);
   }

   menuDb->selectRecordingByGroup->freeResult();

   recordingsStateKey.Remove(false);

   if (Refresh)
      Display();
}

//***************************************************************************
// Load Plain
//***************************************************************************

void cMenuDbRecordings::LoadPlain(bool Refresh)
{
   if (!cRecordings::GetRecordingsRead(recordingsStateKey))
      return ;

   recordingsStateKey.Remove();
   cRecordings* Recordings = cRecordings::GetRecordingsWrite(recordingsStateKey);

   if (!Recordings)
   {
      tell(0, "Fatal: Can't get recording lock");
      return ;
   }

   Clear();

   menuDb->recordingListDb->clear();

   for (int res = menuDb->selectRecordings->find(); res; res = menuDb->selectRecordings->fetch())
   {
      char* fileName {nullptr};
      asprintf(&fileName, "%s/%s", cVideoDirectory::Name(), menuDb->recordingListDb->getStrValue("PATH"));
      const cRecording* recording = Recordings->GetByName(fileName);

      if (recording)
         Add(new cMenuDbRecordingItem(menuDb, recording));
      else
         tell(0, "Fatal: Recording for file '%s' not found", fileName);

      free(fileName);
   }

   menuDb->selectRecordings->freeResult();

   recordingsStateKey.Remove(false);

   if (Refresh)
      Display();
}

//***************************************************************************
// Open
//***************************************************************************

bool cMenuDbRecordings::Open(bool OpenSubMenus)
{
  cMenuDbRecordingItemBase* ri = (cMenuDbRecordingItemBase*)Get(Current());

  if (ri && ri->isDirectory())
  {
     const char* t = ri->Name();
     cString buffer;

     if (base)
     {
        buffer = cString::sprintf("%s%c%s", base, FOLDERDELIMCHAR, t);
        t = buffer;
     }

     AddSubMenu(new cMenuDbRecordings(t, level+1, ri->Name(), OpenSubMenus));

     return true;
  }

  return false;
}

//***************************************************************************
// Play
//***************************************************************************

eOSState cMenuDbRecordings::Play(void)
{
  cMenuDbRecordingItemBase* ri = (cMenuDbRecordingItemBase*)Get(Current());

  if (ri)
  {
     if (ri->isDirectory())
        Open();
     else
     {
        cReplayControl::SetRecording(ri->Recording()->FileName());
        return osReplay;
     }
  }

  return osContinue;
}

//***************************************************************************
// Rewind
//***************************************************************************

eOSState cMenuDbRecordings::Rewind(void)
{
   if (HasSubMenu() || Count() == 0)
      return osContinue;

   cMenuDbRecordingItemBase *ri = (cMenuDbRecordingItemBase*)Get(Current());

   if (ri && !ri->isDirectory())
   {
      cDevice::PrimaryDevice()->StopReplay(); // must do this first to be able to rewind the currently replayed recording
      cResumeFile ResumeFile(ri->Recording()->FileName(), ri->Recording()->IsPesRecording());
      ResumeFile.Delete();
      return Play();
   }

  return osContinue;
}


//***************************************************************************
// Commands
//***************************************************************************

eOSState cMenuDbRecordings::Commands(eKeys Key)
{
   if (HasSubMenu() || Count() == 0)
      return osContinue;

   cMenuDbRecordingItemBase* ri = (cMenuDbRecordingItemBase*)Get(Current());

   if (ri && !ri->isDirectory())
   {
      cMenuCommands *menu;
      eOSState state = AddSubMenu(menu = new cMenuCommands(tr("Recording commands"), &RecordingCommands, cString::sprintf("\"%s\"", *strescape(ri->Recording()->FileName(), "\\\"$"))));

      if (Key != kNone)
         state = menu->ProcessKey(Key);

      return state;
   }

   return osContinue;
}

//***************************************************************************
// Sort
//***************************************************************************

eOSState cMenuDbRecordings::Sort()
{
   LoadPlain();

   return osContinue;
}

//***************************************************************************
// ProcessKey
//***************************************************************************

eOSState cMenuDbRecordings::ProcessKey(eKeys Key)
{
  eOSState state = cOsdMenu::ProcessKey(Key);

  if (state == osUnknown)
  {
     switch (Key)
     {
        case kPlayPause:
        case kPlay:
        case kOk:      return Play();
        case kRed:     return (helpKeys > 1 && RecordingCommands.Count()) ? Commands() : Play();
        case kGreen:   return Rewind();
        case k0:       return Sort();
        case k1:
        case k2:
        case k3:
        case k4:
        case k5:
        case k6:
        case k7:
        case k8:
        case k9: return Commands(Key);

        default: break;
     }
  }

  return state;
}
