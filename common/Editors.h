#pragma once

/* common\Editors.h - the three editors' numbers, once.

   Black, Grey and White each kept its own number in its own source, and Help > About of one could not name the
   others'. Each takes its number from here now - Black's assembly version (GRIDEd\GRIDE\src\Main.cpp), Grey's
   AppInfo::Version (GRIDE 2.0\src\Common\Common.h), White's about::version (GRIDE 1.0\src\about.cpp) - and the
   About box the three share (AboutBox.h) lists all three from the same lines.

   Plain macros, so that C++/CLI and White's native core read the same file. White's number is also the name of its
   former settings folder, "GRIDE 1.0", which Setup's cleanup removes: change it knowing that. */

#define GRIDE_BLACK_VERSION "1.5.0"
#define GRIDE_GREY_VERSION  "2.0.0"
#define GRIDE_WHITE_VERSION "1.0"

/* what each is, in four words, as Help > About lists it after the name and the number */
#define GRIDE_BLACK_WHAT "workspaces, Visual Studio's keys"
#define GRIDE_GREY_WHAT  "one project, White's keys"
#define GRIDE_WHITE_WHAT "The Simple, RIDE's window"
