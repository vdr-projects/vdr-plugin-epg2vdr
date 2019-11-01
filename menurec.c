/*
 * menurec.c: EPG2VDR plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
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
// Class cMenuDbRecordingItem
//***************************************************************************

class cMenuDbRecordingItem : public cOsdItem
{
   public:

      cMenuDbRecordingItem(cMenuDb* db, const cRecording* Recording);
      virtual ~cMenuDbRecordingItem();

      void IncrementCounter(bool New);
      const char* Name()            const { return name; }
      int Level()                   const { return level; }
      const cRecording* Recording() const { return recording; }
      bool IsDirectory()            const { return name; }
      void SetRecording(const cRecording* Recording) { recording = Recording; }
      virtual void SetMenuItem(cSkinDisplayMenu* DisplayMenu, int Index, bool Current, bool Selectable);

   private:

      const cRecording* recording {nullptr};
      int level {0};
      char* name {nullptr};
      int totalEntries {0}, newEntries {0};
      cMenuDb* menuDb {nullptr};
};

cMenuDbRecordingItem::cMenuDbRecordingItem(cMenuDb* db, const cRecording* Recording)
{
   menuDb = db;
   recording = Recording;
   name = nullptr;

   for (const char* p = Recording->Title(); *p; p++)
      if (*p == '~')
         level++;

   SetText(Recording->Title('\t', true, level));
   tell(0, "Added recording for file '%s'", Recording->Title());

   // SetText(menuDb->recordingListDb->getStrValue("TITLE"));

   // a folder?

   if (*Text() == '\t')
      name = strdup(Text() + 2); // 'Text() + 2' to skip the two '\t'
   else
   {
      // -> actual recording

      int Usage = Recording->IsInUse();

      if ((Usage & ruDst) != 0 && (Usage & (ruMove | ruCopy)) != 0)
         SetSelectable(false);
   }
}

cMenuDbRecordingItem::~cMenuDbRecordingItem()
{
   free(name);
}

void cMenuDbRecordingItem::IncrementCounter(bool New)
{
   totalEntries++;

   if (New)
      newEntries++;

   SetText(cString::sprintf("%d\t\t%d\t%s", totalEntries, newEntries, name));
}

void cMenuDbRecordingItem::SetMenuItem(cSkinDisplayMenu *DisplayMenu, int Index, bool Current, bool Selectable)
{
  if (!DisplayMenu->SetItemRecording(recording, Index, Current, Selectable, level, totalEntries, newEntries))
     DisplayMenu->SetItem(Text(), Index, Current, Selectable);
}

//***************************************************************************
// Class cMenuDbRecordings
//***************************************************************************

cString cMenuDbRecordings::path;
cString cMenuDbRecordings::fileName;

cMenuDbRecordings::cMenuDbRecordings(const char* Base, int Level, bool OpenSubMenus)
   : cOsdMenu(Base ? Base : tr("Recordings"), 9, 6, 6)
{
   menuDb = new cMenuDb;

   SetMenuCategory(mcRecording);
   base = Base ? strdup(Base) : nullptr;
   level = Setup.RecordingDirs ? Level : -1;

   Display(); // this keeps the higher level menus from showing up briefly when pressing 'Back' during replay

   if (menuDb->dbConnected())
   {
      LoadPlainList();
   }

   if (Current() < 0)
      SetCurrent(First());

   else if (OpenSubMenus && (cReplayControl::LastReplayed() || *path || *fileName))
   {
      if (!*path || Level < strcountchr(path, FOLDERDELIMCHAR))
      {
         if (Open(true))
            return;
      }
   }

   Display();
   SetHelpKeys();
}

cMenuDbRecordings::~cMenuDbRecordings()
{
   cMenuDbRecordingItem* ri = (cMenuDbRecordingItem*)Get(Current());

   if (ri && !ri->IsDirectory())
      SetRecording(ri->Recording()->FileName());

   delete menuDb;

   free(base);
}

//***************************************************************************
//
//***************************************************************************

void cMenuDbRecordings::SetHelpKeys(void)
{
  cMenuDbRecordingItem* ri = (cMenuDbRecordingItem*)Get(Current());
  int NewHelpKeys = 0;

  if (ri)
  {
     if (ri->IsDirectory())
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
//
//***************************************************************************

void cMenuDbRecordings::LoadPlainList(bool Refresh)
{
   if (!cRecordings::GetRecordingsRead(recordingsStateKey))
      return ;

   recordingsStateKey.Remove();
   // const cRecordings* Recordings = cRecordings::GetRecordingsRead(recordingsStateKey);
   cRecordings *Recordings = cRecordings::GetRecordingsWrite(recordingsStateKey);

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
      {
         // tell(0, "Added recording for file '%s'", fileName);
         Add(new cMenuDbRecordingItem(menuDb, recording));
      }
      else
         tell(0, "Fatal: Recording for file '%s' not found", fileName);

      free(fileName);
   }

   menuDb->selectRecordings->freeResult();

   recordingsStateKey.Remove(false);

   if (Refresh)
      Display();

   return ;

   /*
   // ------------------------------------------------------

   if (!cRecordings::GetRecordingsRead(recordingsStateKey))
      return ;

   recordingsStateKey.Remove();
   const char* CurrentRecording = *fileName ? *fileName : cReplayControl::LastReplayed();
   cRecordings* Recordings = cRecordings::GetRecordingsWrite(recordingsStateKey); // write access is necessary for sorting!
   cMenuDbRecordingItem *LastItem = NULL;

   if (!CurrentRecording)
   {
      if (cMenuDbRecordingItem *ri = (cMenuDbRecordingItem*)Get(Current()))
         CurrentRecording = ri->Recording()->FileName();
   }

   int current = Current();
   Clear();
   GetRecordingsSortMode(DirectoryName());
   Recordings->Sort();

   for (const cRecording *Recording = Recordings->First(); Recording; Recording = Recordings->Next(Recording))
   {
      if ((!filter || filter->Filter(Recording)) && (!base || (strstr(Recording->Name(), base) == Recording->Name() && Recording->Name()[strlen(base)] == FOLDERDELIMCHAR)))
      {
         cMenuDbRecordingItem *Item = new cMenuDbRecordingItem(Recording, level);
         cMenuDbRecordingItem *LastDir = NULL;

         if (Item->IsDirectory())
         {
            // Sorting may ignore non-alphanumeric characters, so we need to explicitly handle directories in case they only differ in such characters:

            for (cMenuDbRecordingItem* p = LastItem; p; p = dynamic_cast<cMenuDbRecordingItem*>(p->Prev()))
            {
               if (p->Name() && strcmp(p->Name(), Item->Name()) == 0)
               {
                  LastDir = p;
                  break;
               }
            }
         }

         if (*Item->Text() && !LastDir)
         {
            Add(Item);
            LastItem = Item;

            if (Item->IsDirectory())
               LastDir = Item;
         }
         else
            delete Item;

         if (LastItem || LastDir)
         {
            if (*path)
            {
               if (strcmp(path, Recording->Folder()) == 0)
                  SetCurrent(LastDir ? LastDir : LastItem);
            }
            else if (CurrentRecording && strcmp(CurrentRecording, Recording->FileName()) == 0)
               SetCurrent(LastDir ? LastDir : LastItem);
         }

         if (LastDir)
            LastDir->IncrementCounter(Recording->IsNew());
      }
   }

   if (Current() < 0)
      SetCurrent(Get(current));      // last resort, in case the recording was deleted

   SetMenuSortMode(RecordingsSortMode == rsmName ? msmName : msmTime);
   recordingsStateKey.Remove(false); // sorting doesn't count as a real modification

   if (Refresh)
      Display();
   */
}

//***************************************************************************
//
//***************************************************************************

void cMenuDbRecordings::SetPath(const char *Path)
{
   path = Path;
}

void cMenuDbRecordings::SetRecording(const char *FileName)
{
   fileName = FileName;
}

cString cMenuDbRecordings::DirectoryName(void)
{
  cString d(cVideoDirectory::Name());

  if (base)
  {
     char *s = ExchangeChars(strdup(base), true);
     d = AddDirectory(d, s);
     free(s);
  }

  return d;
}

bool cMenuDbRecordings::Open(bool OpenSubMenus)
{
  cMenuDbRecordingItem* ri = (cMenuDbRecordingItem*)Get(Current());

  if (ri && ri->IsDirectory() && (!*path || strcountchr(path, FOLDERDELIMCHAR) > 0))
  {
     const char* t = ri->Name();
     cString buffer;

     if (base)
     {
        buffer = cString::sprintf("%s%c%s", base, FOLDERDELIMCHAR, t);
        t = buffer;
     }

     AddSubMenu(new cMenuDbRecordings(t, level + 1, OpenSubMenus));
     return true;
  }

  return false;
}

eOSState cMenuDbRecordings::Play(void)
{
  cMenuDbRecordingItem* ri = (cMenuDbRecordingItem*)Get(Current());

  if (ri)
  {
     if (ri->IsDirectory())
        Open();
     else
     {
        cReplayControl::SetRecording(ri->Recording()->FileName());
        return osReplay;
     }
  }

  return osContinue;
}

eOSState cMenuDbRecordings::Rewind(void)
{
   if (HasSubMenu() || Count() == 0)
      return osContinue;

   cMenuDbRecordingItem *ri = (cMenuDbRecordingItem *)Get(Current());

   if (ri && !ri->IsDirectory())
   {
      cDevice::PrimaryDevice()->StopReplay(); // must do this first to be able to rewind the currently replayed recording
      cResumeFile ResumeFile(ri->Recording()->FileName(), ri->Recording()->IsPesRecording());
      ResumeFile.Delete();
      return Play();
   }

  return osContinue;
}

/*
eOSState cMenuDbRecordings::Delete(void)
{
  if (HasSubMenu() || Count() == 0)
     return osContinue;
  cMenuDbRecordingItem *ri = (cMenuDbRecordingItem *)Get(Current());
  if (ri && !ri->IsDirectory()) {
     if (Interface->Confirm(tr("Delete recording?"))) {
        if (cRecordControl *rc = cRecordControls::GetRecordControl(ri->Recording()->FileName())) {
           if (Interface->Confirm(tr("Timer still recording - really delete?"))) {
              if (cTimer *Timer = rc->Timer()) {
                 LOCK_TIMERS_WRITE;
                 Timer->Skip();
                 cRecordControls::Process(Timers, time(NULL));
                 if (Timer->IsSingleEvent()) {
                    Timers->Del(Timer);
                    isyslog("deleted timer %s", *Timer->ToDescr());
                    }
                 }
              }
           else
              return osContinue;
           }
        cString FileName;
        {
          LOCK_RECORDINGS_READ;
          if (const cRecording *Recording = Recordings->GetByName(ri->Recording()->FileName())) {
             FileName = Recording->FileName();
             if (RecordingsHandler.GetUsage(FileName)) {
                if (!Interface->Confirm(tr("Recording is being edited - really delete?")))
                   return osContinue;
                }
             }
        }
        RecordingsHandler.Del(FileName); // must do this w/o holding a lock, because the cleanup section in cDirCopier::Action() might request one!
        if (cReplayControl::NowReplaying() && strcmp(cReplayControl::NowReplaying(), FileName) == 0)
           cControl::Shutdown();
        cRecordings *Recordings = cRecordings::GetRecordingsWrite(recordingsStateKey);
        Recordings->SetExplicitModify();
        cRecording *Recording = Recordings->GetByName(FileName);
        if (!Recording || Recording->Delete()) {
           cReplayControl::ClearLastReplayed(FileName);
           Recordings->DelByName(FileName);
           cOsdMenu::Del(Current());
           SetHelpKeys();
           cVideoDiskUsage::ForceCheck();
           Recordings->SetModified();
           recordingsStateKey.Remove();
           Display();
           if (!Count())
              return osUserRecEmpty;
           return osUserRecRemoved;
           }
        else
           Skins.Message(mtError, tr("Error while deleting recording!"));
        recordingsStateKey.Remove();
        }
     }
  return osContinue;
}
*/
/*
eOSState cMenuDbRecordings::Info(void)
{
  if (HasSubMenu() || Count() == 0)
     return osContinue;
  if (cMenuDbRecordingItem *ri = (cMenuDbRecordingItem *)Get(Current())) {
     if (ri->IsDirectory())
        return AddSubMenu(new cMenuPathEdit(cString(ri->Recording()->Name(), strchrn(ri->Recording()->Name(), FOLDERDELIMCHAR, ri->Level() + 1))));
     else
        return AddSubMenu(new cMenuRecording(ri->Recording(), true));
     }
  return osContinue;
}
*/
eOSState cMenuDbRecordings::Commands(eKeys Key)
{
   if (HasSubMenu() || Count() == 0)
      return osContinue;

   cMenuDbRecordingItem *ri = (cMenuDbRecordingItem *)Get(Current());

   if (ri && !ri->IsDirectory())
   {
      cMenuCommands *menu;
      eOSState state = AddSubMenu(menu = new cMenuCommands(tr("Recording commands"), &RecordingCommands, cString::sprintf("\"%s\"", *strescape(ri->Recording()->FileName(), "\\\"$"))));

      if (Key != kNone)
         state = menu->ProcessKey(Key);

      return state;
   }

   return osContinue;
}

eOSState cMenuDbRecordings::Sort()
{
   LoadPlainList();

   return osContinue;
}

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
        // case kYellow: return Delete();
        case kInfo:
        // case kBlue:   return Info();
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

  /*
  else if (state == osUserRecRenamed) {
     // a recording was renamed (within the same folder), so let's refresh the menu
     CloseSubMenu(false); // this is the cMenuRecordingEdit/cMenuPathEdit
     path = NULL;
     fileName = NULL;
     state = osContinue;
     }
  else if (state == osUserRecMoved) {
     // a recording was moved to a different folder, so let's delete the old item
     CloseSubMenu(false); // this is the cMenuRecordingEdit/cMenuPathEdit
     path = NULL;
     fileName = NULL;
     cOsdMenu::Del(Current());
     Set(); // the recording might have been moved into a new subfolder of this folder
     if (!Count())
        return osUserRecEmpty;
     Display();
     state = osUserRecRemoved;
     }
  else if (state == osUserRecRemoved) {
     // a recording was removed from a sub folder, so update the current item
     if (cOsdMenu *m = SubMenu()) {
        if (cMenuDbRecordingItem *ri = (cMenuDbRecordingItem *)Get(Current())) {
           if (cMenuDbRecordingItem *riSub = (cMenuDbRecordingItem *)m->Get(m->Current()))
              ri->SetRecording(riSub->Recording());
           }
        }
     // no state change here, this report goes upstream!
     }
  else if (state == osUserRecEmpty) {
     // a subfolder became empty, so let's go back up
     CloseSubMenu(false); // this is the now empty submenu
     cOsdMenu::Del(Current()); // the menu entry of the now empty subfolder
     Set(); // in case a recording was moved into a new subfolder of this folder
     if (base && !Count()) // base: don't go up beyond the top level Recordings menu
        return state;
     Display();
     state = osContinue;
     }
  */

  // if (!HasSubMenu())
  // {
  //    Set(true);

  //    if (Key != kNone)
  //       SetHelpKeys();
  // }

  return state;
}
