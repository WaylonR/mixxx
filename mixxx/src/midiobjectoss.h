/***************************************************************************
                          midiobjectoss.h  -  description
                             -------------------
    begin                : Thu Jul 4 2002
    copyright            : (C) 2002 by Tue & Ken Haste Andersen
    email                : haste@diku.dk
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef MIDIOBJECTOSS_H
#define MIDIOBJECTOSS_H

#include "midiobject.h"

/**
  *@author Tue & Ken Haste Andersen
  */

class MidiObjectOSS : public MidiObject  {
public: 
    MidiObjectOSS(ConfigObject<ConfigValueMidi> *c, QApplication *app, ControlObject *control, QString device);
    ~MidiObjectOSS();
    void devOpen(QString device);
    void devClose();
protected:
    void run();
    void stop();
    int thread_pid;


    int     handle;
    unsigned char    *buffer;
};

#endif
