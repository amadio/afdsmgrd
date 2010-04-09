#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>


#include <TDatime.h>
#include <TSystem.h>

#include "AfLog.h"

AfLog::AfLog() {
  kFallbackLogFile = stderr;
  fDatime = new TDatime();
  fLastRotated = NULL;
  SetStdErr();
}

AfLog::~AfLog() {
  if (fLastRotated) {
    delete fLastRotated;
  }
  delete fDatime;
}

void AfLog::Init() {
  if (!gLog) {
    gLog = new AfLog();
    gLog->fLogFile = gLog->kFallbackLogFile;
  }
  else {
    gLog->Warning("Log facility already initialized!");
  }
}

bool AfLog::SetFile(const char *fn) {
  fLogFile = fopen(fn, "a");
  if (!fLogFile) {
    SetStdErr();
    return false;
  }

  fLogFileName = fn;

  if (fLastRotated) {
    fLastRotated->Set();
  }
  else {
    fLastRotated = new TDatime();
  }
  fRotateable = true;
  return true;
}

void AfLog::SetStdErr() {
  fLogFile = kFallbackLogFile;
  fLogFileName = NULL;
  fRotateable = false;
  if (fLastRotated) {
    delete fLastRotated;
    fLastRotated = NULL;
  }
}

int AfLog::CheckRotate() {

  if (!fRotateable) {
    return 0;
  }

  fDatime->Set();

  if ( fDatime->GetDate() > fLastRotated->GetDate() ) {
  //if ( fDatime->GetTime() > fLastRotated->GetTime() ) {
    char buf[200];
    fDatime->Set( fDatime->Convert() - 1 );
    snprintf(buf, 200, "%s.%04u%02u%02u", fLogFileName, fDatime->GetYear(),
      fDatime->GetMonth(), fDatime->GetDay());

    //snprintf(buf, 200, "%s.%04u%02u%02u-%02u%02u%02u", fLogFileName,
    //  fDatime->GetYear(), fDatime->GetMonth(), fDatime->GetDay(),
    //  fDatime->GetHour(), fDatime->GetMinute(), fDatime->GetSecond());

    fclose(fLogFile);
    int rRen = gSystem->Rename( fLogFileName, buf );
    // TODO: add compression here

    bool rSet = SetFile(fLogFileName);

    if ((rRen == -1) || (!rSet)) {
      return -1;
    }

    return 1;
  }

}

void AfLog::Message(msgType type, const char *fmt, va_list args) {
  int r = CheckRotate();
  va_list dummy = {};
  if (r == -1) {
    Format(kAfError, "Can't rotate logfile!", dummy);
  }
  else if (r == 1) {
    Format(kAfOk, "Logfile rotated", dummy);
  }
  // 0 == no need to rotate
  Format(type, fmt, args);
}

void AfLog::Format(msgType type, const char *fmt, va_list args) {

  char prefix[4];

  switch (type) {
    case kAfOk:
      strcpy(prefix, "OK!");
    break;
    case kAfWarning:
      strcpy(prefix, "WRN");
    break;
    case kAfInfo:
      strcpy(prefix, "INF");
    break;
    case kAfError:
      strcpy(prefix, "ERR");
    break;
    case kAfFatal:
      strcpy(prefix, "FTL");
    break;
  }
  fDatime->Set();
  fprintf(fLogFile, "[%s] *** %s *** ", fDatime->AsSQLString(), prefix);
  vfprintf(fLogFile, fmt, args);
  fputc('\n', fLogFile);
  fflush(fLogFile);
}

void AfLog::Info(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Message(kAfInfo, fmt, args);
  va_end(args);
}

void AfLog::Ok(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Message(kAfOk, fmt, args);
  va_end(args);
}

void AfLog::Warning(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Message(kAfWarning, fmt, args);
  va_end(args);
}

void AfLog::Error(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Message(kAfError, fmt, args);
  va_end(args);
}

void AfLog::Fatal(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Message(kAfFatal, fmt, args);
  va_end(args);
}
