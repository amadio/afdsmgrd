#include "AfDataSetsManager.h"

AfDataSetsManager::AfDataSetsManager() {
  fSrcList = new TList();
  fSrcList->SetOwner();
  fSuid = false;
  fReset = false;
  fLoopSleep_s = kDefaultLoopSleep_s;
  fScanDsEvery = kDefaultScanDsEvery;
  fParallelXfrs = kDefaultParallelXfrs;
  fStageQueue = new TList();
  fStageQueue->SetOwner();
  fStageCmds = new TList();  // not owner, threads must be cancelled manually
}

AfDataSetsManager::~AfDataSetsManager() {
  delete fSrcList;
  delete fStageQueue;
}

Bool_t AfDataSetsManager::ReadConf(const char *cf) {

  AfConfReader *cfr = AfConfReader::Open(cf);

  //cfr->PrintVarsAndDirs();

  if (!cfr) {
    AfLogFatal("Cannot read configuration file: %s", cf);
    return kFALSE;
  }

  // List is emptied before refilling it
  fSrcList->Clear();

  // Loop pause
  TString *loopSleep_s = cfr->GetDir("dsmgrd.sleepsecs");
  if (!loopSleep_s) {
    AfLogWarning("Variable dsmgrd.sleepsecs not set, using default (%d)",
      kDefaultLoopSleep_s);
    fLoopSleep_s = kDefaultLoopSleep_s;
  }
  else if ( (fLoopSleep_s = loopSleep_s->Atoi()) <= 0 )  {
    AfLogWarning("Invalid value for dsmgrd.sleepsecs (%s), using default (%d)",
      loopSleep_s->Data(), kDefaultLoopSleep_s);
    fLoopSleep_s = kDefaultLoopSleep_s;
    delete loopSleep_s;
  }
  else {
    AfLogInfo("Sleep between scans set to %d seconds", fLoopSleep_s);
    delete loopSleep_s;
  }

  // Scan dataset every X loops
  TString *scanDsEvery = cfr->GetDir("dsmgrd.scandseveryloops");
  if (!scanDsEvery) {
    AfLogWarning("Variable dsmgrd.scandseveryloops not set, using default (%d)",
      kDefaultScanDsEvery);
    fScanDsEvery = kDefaultScanDsEvery;
  }
  else if ( (fScanDsEvery = scanDsEvery->Atoi()) <= 0 )  {
    AfLogWarning("Invalid value for dsmgrd.scandseveryloops (%s), using "
      "default (%d)", scanDsEvery->Data(), kDefaultScanDsEvery);
    fScanDsEvery = kDefaultScanDsEvery;
    delete scanDsEvery;
  }
  else {
    AfLogInfo("Datasets are checked every %d loops", fScanDsEvery);
    delete scanDsEvery;
  }

  // Number of parallel staging command on the whole facility
  TString *parallelXfrs = cfr->GetDir("dsmgrd.parallelxfrs");
  if (!parallelXfrs) {
    AfLogWarning("Variable dsmgrd.parallelxfrs not set, using default (%d)",
      kDefaultParallelXfrs);
    fParallelXfrs = kDefaultParallelXfrs;
  }
  else if ( (fParallelXfrs = parallelXfrs->Atoi()) <= 0 )  {
    AfLogWarning("Invalid value for dsmgrd.parallelxfrs (%s), using "
      "default (%d)", parallelXfrs->Data(), kDefaultParallelXfrs);
    fParallelXfrs = kDefaultParallelXfrs;
    delete parallelXfrs;
  }
  else {
    AfLogInfo("Number of parallel staging commands set to %d", fParallelXfrs);
    delete parallelXfrs;
  }

  // Stage command
  TString *stageCmd = cfr->GetDir("dsmgrd.stagecmd");
  if (stageCmd) {
    fStageCmd = *stageCmd;
    delete stageCmd;
    AfLogInfo("Staging command is %s", fStageCmd.Data());
  }
  else {
    AfLogFatal("No stage command specified.");
    delete stageCmd;
    return kFALSE;
  }

  // Which datasets to process? The format is the same of TDataSetManagerFile
  TList *procDs = cfr->GetDirs("dsmgrd.processds");
  TIter j( procDs );
  TObjString *o;

  while ( o = dynamic_cast<TObjString *>(j.Next()) ) {
    AfLogInfo("Dataset mask to process: %s", o->GetString().Data());
  }

  // Parse dataset sources

  TPMERegexp reMss("([ \t]|^)mss:([^ \t]*)");  // regex that matches mss:
  TPMERegexp reUrl("([ \t]|^)url:([^ \t]*)");  // regex that matches url:
  TPMERegexp reOpt("([ \t]|^)opt:([^ \t]*)");  // regex that matches opt:
  TPMERegexp reDsp("([ \t]|^)destpath:([^ \t]*)");
  TPMERegexp reRw("([ \t]|^)rw=1([^ \t]*)");

  // Watch out: getDirs returns a poiter to a TList that must be deleted, and it
  // is owner of its content!
  TList *dsSrcList = cfr->GetDirs("xpd.datasetsrc");
  TIter i( dsSrcList );

  while ( o = dynamic_cast<TObjString *>(i.Next()) ) {

    TString dir = o->GetString();

    AfLogInfo("Found dataset configuration: %s", dir.Data());

    Bool_t dsValid = kTRUE;
    TUrl *redirUrl;
    TString destPath;
    TString dsUrl;
    TString opts;
    Bool_t rw = kFALSE;

    if (reMss.Match(dir) == 3) {

      redirUrl = new TUrl(reMss[2]);

      if ((!redirUrl->IsValid()) ||
        (strcmp(redirUrl->GetProtocol(), "root") != 0)) {
        AfLogError(">> Invalid MSS URL: only URLs in the form " \
          "root://host[:port] are supported");
        delete redirUrl;
        dsValid = kFALSE;
      }
      else {
        // URL is "flattened": only proto, host and port are retained
        redirUrl->SetUrl( Form("%s://%s:%d", redirUrl->GetProtocol(),
          redirUrl->GetHost(), redirUrl->GetPort()) );
        AfLogInfo(">> MSS: %s", redirUrl->GetUrl());
      }
    }
    else {
      AfLogError(">> MSS URL not set");
      dsValid = kFALSE;
    }

    if (reDsp.Match(dir) == 3) {
      destPath = reDsp[2];
      AfLogInfo(">> Destination path: %s", destPath.Data());
    }
    else {
      AfLogWarning(">> Destination path on pool (destpath) not set, using " \
        "default (/alien)");
      destPath = "/alien";
    }

    if (reUrl.Match(dir) == 3) {
      dsUrl = reUrl[2];
      AfLogInfo(">> URL: %s", dsUrl.Data());
    }
    else {
      AfLogError(">> Dataset local path not set");
      dsValid = kFALSE;
    }

    if (reRw.Match(dir) == 3) {
      AfLogInfo(">> R/W: yes");
      rw = kTRUE;
    }

    if (reOpt.Match(dir) == 3) {
      opts = reOpt[2];
      AfLogInfo(">> Opt: %s", opts.Data());
    }
    else {
      // Default options: do not allow register and verify if readonly
      opts = rw ? "Ar:Av:" : "-Ar:-Av:";
    }

    if (dsValid) {
      redirUrl->SetFile( destPath.Data() );
      AfDataSetSrc *dsSrc = new AfDataSetSrc(dsUrl.Data(), redirUrl,
        opts.Data(), fSuid, this);
      if (procDs->GetSize() > 0) {
        dsSrc->SetDsProcessList(procDs);  // Set... makes a copy of the list
      }
      fSrcList->Add(dsSrc);
    }
    else {
      AfLogError(">> Invalid dataset configuration, ignoring", *i);
    }

  } // for over xpd.datasetsrc

  delete dsSrcList;
  delete cfr;
  delete procDs;

  if (fSrcList->GetSize() == 0) {
    AfLogFatal("No valid dataset source found!");
    return kFALSE;
  }

  return kTRUE;
}

void AfDataSetsManager::Loop(Bool_t runOnce) {

  Int_t loops = fScanDsEvery;

  while (kTRUE) {

    AfLogDebug("++++ Started loop over transfer queue ++++");
    PrintStageList();
    ProcessTransferQueue();
    PrintStageList();
    AfLogDebug("++++ Loop over transfer queue completed ++++");

    if (loops++ == fScanDsEvery) {

      TIter i(fSrcList);
      AfDataSetSrc *dsSrc;

      AfLogDebug("++++ Started loop over dataset sources ++++");
      while ( dsSrc = dynamic_cast<AfDataSetSrc *>(i.Next()) ) {
        dsSrc->Process(fReset);
      }
      AfLogDebug("++++ Loop over dataset sources completed ++++");

      if (runOnce) {
        break;
      }

      loops = 0;
    }
    else {
      AfLogDebug("Not scanning datasets now: %d sleep(s) left before a new "
        "dataset scan", fScanDsEvery-loops+1);
    }

    AfLogDebug("Sleeping %d seconds before a new loop", fLoopSleep_s),
    gSystem->Sleep(fLoopSleep_s * 1000);
  }
}

StgStatus_t AfDataSetsManager::GetStageStatus(const char *url) {

  AfStageUrl search(url);
  AfStageUrl *found =
    dynamic_cast<AfStageUrl *>( fStageQueue->FindObject(&search) );

  if (found == NULL) {
    return kStgAbsent;
  }

  return found->GetStageStatus();
}

Bool_t AfDataSetsManager::EnqueueUrl(const char *url) {

  AfStageUrl search(url);

  // Only adds elements not already there
  if ( fStageQueue->FindObject( &search ) == NULL ) {
    fStageQueue->AddLast( new AfStageUrl(url) );
    return kTRUE;
  }

  return kFALSE;
}

Bool_t AfDataSetsManager::DequeueUrl(const char *url) {

  AfStageUrl search(url);

  if (fStageQueue->Remove( &search )) {
    return kTRUE;
  }

  return kFALSE;
}

void AfDataSetsManager::PrintStageList() {

  if (!gLog->GetDebug()) {
    return;
  }

  TIter i( fStageQueue );
  AfStageUrl *su;

  AfLogDebug("Staging queue has %d element(s) (D=done, Q=queued, S=staging, "
    "F=fail):",
    fStageQueue->GetSize());
  while ( su = dynamic_cast<AfStageUrl *>(i.Next()) ) {
    AfLogDebug(">> %c | %s", su->GetStageStatus(), su->GetUrl().Data());
  }

}

void AfDataSetsManager::ProcessTransferQueue() {

  // Loop over all elements:
  //
  // If an element is Q:
  //  - start a thread if enough threads are free
  //  - change its status into S
  //
  // If an element is S:
  //  - check if corresponding thread has finished
  //    - purge thread (join + delete)
  //    - change status to DONE or FAILED (it depends)
  //    - delete thread from the list
  //
  // In any other case (D, F) ignore it: the list must be cleaned up by the
  // single dataset manager accordingly.

  TIter i( fStageQueue );
  AfStageUrl *su;

  // First loop: only look for elements "staging" and check if their
  // corresponding thread has finished
  while ( su = dynamic_cast<AfStageUrl *>(i.Next()) ) {

    TString url = su->GetUrl();

    if (su->GetStageStatus() == kStgStaging) {

      TThread *t = dynamic_cast<TThread *>(fStageCmds->FindObject(url.Data()));
      if (t) {

        if (t->GetState() == TThread::kRunningState) {
          AfLogDebug("Thread #%ld running for staging %s", t->GetId(),
            url.Data());
        }
        else if (t->GetState() == TThread::kCanceledState) {

          AfLogDebug("Thread #%ld completed", t->GetId());

          Bool_t *retVal = NULL;
          Long_t l = t->Join((void **)&retVal);

          if (*retVal) {
            AfLogOk("Staging completed: %s", url.Data());
            su->SetStageStatus(kStgDone);
          }
          else {
            AfLogError("Staging failed: %s", url.Data());
            su->SetStageStatus(kStgFail);
          }

          delete retVal;

          if (!fStageCmds->Remove(t)) {
            AfLogError(">>>> Failed removing staging thread from list - this "
              "should not happen!");
          }

          //t->Delete(); // safe to use delete: thread has been cancelled
          delete t;
        }

      }
      else {
        AfLogError("Can't find thread associated to transfer %s - this should "
          "not happen!", url.Data());
      }
    }

  }

  i.Reset();

  // Second loop: only look for elements "queued" and start thread accordingly
  while ( su = dynamic_cast<AfStageUrl *>(i.Next()) ) {

    Int_t nXfr = fStageCmds->GetSize();

    if ((su->GetStageStatus() == kStgQueue) && (nXfr < fParallelXfrs)) {

      TString url = su->GetUrl();

      // Arguments for the staging command: this TObjArray is owner
      TObjArray *args = new TObjArray(2);  // capacity=2
      args->SetOwner();
      args->AddAt(new TObjString(url), 0);  // the url
      args->AddAt(new TObjString(fStageCmd), 1);     // the cmd

      TThread *t = new TThread(url.Data(), (TThread::VoidRtnFunc_t)&Stage,
        args);
      fStageCmds->AddLast(t);
      t->Run();
      su->SetStageStatus(kStgStaging);
      AfLogInfo("Staging started: %s", url.Data());
      AfLogDebug("Spawned thread #%ld to stage %s", t->GetId(), url.Data());
    }
  }

}

void *AfDataSetsManager::Stage(void *args) {
  TObjArray *o = (TObjArray *)args;
  TString url = dynamic_cast<TObjString *>( o->At(0) )->GetString();
  TString cmd = dynamic_cast<TObjString *>( o->At(1) )->GetString();
  delete o;  // owner: TObjStrings are also deleted

  TPMERegexp re("\\$URLTOSTAGE([ \t]|$)", "g");
  url.Append(" ");
  if (re.Substitute(cmd, url, kFALSE) == 0) {
    // If no URLTOSTAGE is found, URL is appended at the end of command by def.
    cmd.Append(" ");
    cmd.Append(url);
  }
  cmd.Append("2> /dev/null");  // we are only interested in stdout

  AfLogDebug("Thread #%ld is spawning staging command: %s", TThread::SelfId(),
    cmd.Data());

  TString out = gSystem->GetFromPipe(cmd.Data());

  Bool_t *retVal = new Bool_t;
  if (out.BeginsWith("OK ")) {
    *retVal = kTRUE;
  }
  else {
    *retVal = kFALSE;
  }

  return retVal;
}
