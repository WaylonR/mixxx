/***************************************************************************
                          engineclipping.h  -  description
                             -------------------
    copyright            : (C) 2002 by Tue and Ken Haste Andersen
    email                :
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef ENGINECLIPPING_H
#define ENGINECLIPPING_H

#include "engineobject.h"

class ControlEngine;

class EngineClipping : public EngineObject {
 public:
    EngineClipping(const char *group);
    ~EngineClipping();
    void notify(double) {};
    CSAMPLE *process(const CSAMPLE *, const int);
 private:
    ControlEngine *bulb;

    CSAMPLE *buffer;
};

#endif


















