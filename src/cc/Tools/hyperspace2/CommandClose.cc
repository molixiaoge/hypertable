/**
 * Copyright (C) 2007 Doug Judd (Zvents, Inc.)
 * 
 * This file is part of Hypertable.
 * 
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 * 
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <iostream>

#include "Common/Error.h"
#include "Common/Usage.h"

#include "CommandClose.h"
#include "Global.h"

using namespace hypertable;
using namespace Hyperspace;
using namespace std;

const char *CommandClose::msUsage[] = {
  "close <file>",
  "  This command issues a CLOSE request to Hyperspace.",
  (const char *)0
};

int CommandClose::run() {
  uint64_t handle;
  int error;

  if (mArgs.size() != 1) {
    cerr << "Wrong number of arguments.  Type 'help' for usage." << endl;
    return -1;
  }

  if (mArgs[0].second != "") {
    cerr << "Invalid character '=' in argument." << endl;
    return -1;
  }

  Global::FileMapT::iterator iter = Global::fileMap.find(mArgs[0].first);
  if (iter == Global::fileMap.end()) {
    LOG_VA_ERROR("Unable to find '%s' in open file map", mArgs[0].first.c_str());
    return -1;
  }
  handle = (*iter).second;

  if ((error = mSession->Close(handle)) != Error::OK) {
    LOG_VA_ERROR("Error executing CLOSE request - %s", Error::GetText(error));
  }

  return error;
}